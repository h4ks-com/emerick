// The main-thread side of a VST3 plugin: loading its module, creating its component and controller, activation,
// state and params.

#include "vst3_plugin.hpp"

#include <pluginterfaces/base/funknownimpl.h>
#include <pluginterfaces/vst/vstspeaker.h>
#include <public.sdk/source/common/memorystream.h>
#include <public.sdk/source/vst/utility/stringconvert.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <format>
#include <map>
#include <ranges>
#include <span>
#include <thread>

#include "vst3_host.hpp"

namespace emerick::engine {

namespace {

// A plugin that stopped processing without telling us leaves the queue unread, so we flush it ourselves after this.
constexpr auto kParamWait = std::chrono::milliseconds(50);
constexpr double kIdleSampleRate = 48000.0;
constexpr core::Frames kIdleMaxFrames = 1024;
// Our state blob starts with this tag, then the component's state and the controller's, each after its byte count.
constexpr std::array<std::uint8_t, 4> kStateTag{'E', 'V', 'S', '1'};
constexpr Steinberg::TBool kOn = 1;
constexpr Steinberg::TBool kOff = 0;

std::map<std::filesystem::path, std::weak_ptr<VST3::Hosting::Module>>& openModules() {
    static std::map<std::filesystem::path, std::weak_ptr<VST3::Hosting::Module>> modules;
    return modules;
}

std::expected<VST3::Hosting::Module::Ptr, std::string> openModule(const std::filesystem::path& bundle) {
    auto& modules = openModules();
    if (auto alive = modules[bundle].lock()) {
        return alive;
    }
    std::string error;
    auto module = VST3::Hosting::Module::create(bundle.string(), error);
    if (!module) {
        return std::unexpected("cannot load " + bundle.string() + ": " + error);
    }
    modules[bundle] = module;
    return module;
}

// Each bus gets buffers for its channels, so the audio thread never allocates.
void allocateBuses(vst::IComponent& component, vst::BusDirection direction, core::Frames maxFrames,
                   std::vector<BusBuffers>& buffers, std::vector<vst::AudioBusBuffers>& buses) {
    const auto count = static_cast<std::size_t>(std::max(component.getBusCount(vst::kAudio, direction), 0));
    buffers.assign(count, {});
    buses.assign(count, {});
    for (std::size_t index = 0; index < count; ++index) {
        vst::BusInfo info{};
        component.getBusInfo(vst::kAudio, direction, static_cast<Steinberg::int32>(index), info);
        BusBuffers& bus = buffers[index];
        bus.channels.assign(static_cast<std::size_t>(std::max(info.channelCount, 0)),
                            std::vector<float>(static_cast<std::size_t>(maxFrames)));
        bus.pointers = bus.channels |
                       std::views::transform([](std::vector<float>& channel) { return channel.data(); }) |
                       std::ranges::to<std::vector>();
        buses[index].numChannels = info.channelCount;
        buses[index].silenceFlags = 0;
        // AudioBusBuffers keeps its channels in a union, and we set up 32-bit processing.
        buses[index].channelBuffers32 = bus.pointers.data();  // NOLINT(cppcoreguidelines-pro-type-union-access)
    }
}

// We ask for stereo on the main buses and take what the plugin keeps when it refuses.
void arrangeBuses(vst::IAudioProcessor& processor, vst::IComponent& component) {
    const bool input = component.getBusCount(vst::kAudio, vst::kInput) > 0;
    const bool output = component.getBusCount(vst::kAudio, vst::kOutput) > 0;
    vst::SpeakerArrangement in = vst::SpeakerArr::kStereo;
    vst::SpeakerArrangement out = vst::SpeakerArr::kStereo;
    processor.setBusArrangements(input ? &in : nullptr, input ? 1 : 0, output ? &out : nullptr, output ? 1 : 0);
    if (input) {
        component.activateBus(vst::kAudio, vst::kInput, 0, kOn);
    }
    if (output) {
        component.activateBus(vst::kAudio, vst::kOutput, 0, kOn);
    }
}

void writeSize(std::vector<std::uint8_t>& out, std::uint32_t size) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>(size >> shift));
    }
}

std::optional<std::uint32_t> readSize(std::span<const std::uint8_t>& in) {
    if (in.size() < 4) {
        return std::nullopt;
    }
    std::uint32_t size = 0;
    for (int index = 0; index < 4; ++index) {
        size |= static_cast<std::uint32_t>(in[static_cast<std::size_t>(index)]) << (8 * index);
    }
    in = in.subspan(4);
    return size;
}

std::optional<std::span<const std::uint8_t>> readPart(std::span<const std::uint8_t>& in) {
    const auto size = readSize(in);
    if (!size || *size > in.size()) {
        return std::nullopt;
    }
    const auto part = in.first(*size);
    in = in.subspan(*size);
    return part;
}

// The SDK's stream reads memory it does not own through a mutable pointer, and never writes to it.
Steinberg::MemoryStream readStream(std::span<const std::uint8_t> bytes) {
    return {const_cast<std::uint8_t*>(bytes.data()),  // NOLINT(cppcoreguidelines-pro-type-const-cast)
            static_cast<Steinberg::TSize>(bytes.size())};
}

// The SDK's stream keeps its bytes as char, which we read as the bytes they are.
std::span<const std::uint8_t> bytesOf(const Steinberg::MemoryStream& stream) {
    return {reinterpret_cast<const std::uint8_t*>(stream.getData()), static_cast<std::size_t>(stream.getSize())};
}

}  // namespace

Steinberg::tresult PLUGIN_API ComponentHandler::performEdit(vst::ParamID id, vst::ParamValue value) {
    if (writeRecord(impl_->paramQueue, Vst3ParamChange{.id = id, .value = value})) {
        ++impl_->paramsSent;
    }
    impl_->stateDirty.store(true, std::memory_order_relaxed);
    impl_->paramsChanged.store(true, std::memory_order_relaxed);
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API ComponentHandler::restartComponent(Steinberg::int32 flags) {
    if ((flags & (vst::kLatencyChanged | vst::kIoChanged | vst::kReloadComponent)) != 0) {
        impl_->restartRequested.store(true, std::memory_order_relaxed);
    }
    if ((flags & (vst::kParamValuesChanged | vst::kParamTitlesChanged)) != 0) {
        impl_->paramsChanged.store(true, std::memory_order_relaxed);
    }
    return Steinberg::kResultOk;
}

void Vst3Plugin::Impl::deactivate() {
    if (active) {
        processor->setProcessing(kOff);
        component->setActive(kOff);
        active = false;
    }
}

bool Vst3Plugin::Impl::activateAt(double rate, core::Frames frames) {
    deactivate();
    arrangeBuses(*processor, *component);
    if (component->getBusCount(vst::kEvent, vst::kInput) > 0) {
        component->activateBus(vst::kEvent, vst::kInput, 0, kOn);
        takesNotes = true;
    }
    allocateBuses(*component, vst::kInput, frames, inputs, inputBuses);
    allocateBuses(*component, vst::kOutput, frames, outputs, outputBuses);
    sampleRate = rate;
    maxFrames = frames;
    vst::ProcessSetup setup{.processMode = vst::kRealtime,
                            .symbolicSampleSize = vst::kSample32,
                            .maxSamplesPerBlock = static_cast<Steinberg::int32>(frames),
                            .sampleRate = rate};
    if (processor->canProcessSampleSize(vst::kSample32) != Steinberg::kResultOk ||
        processor->setupProcessing(setup) != Steinberg::kResultOk ||
        component->setActive(kOn) != Steinberg::kResultOk) {
        return false;
    }
    // Many plugins return kNotImplemented here and process anyway.
    processor->setProcessing(kOn);
    latency = static_cast<core::Frames>(processor->getLatencySamples());
    active = true;
    return true;
}

void Vst3Plugin::Impl::flushParams() {
    if (!active &&
        !activateAt(sampleRate > 0.0 ? sampleRate : kIdleSampleRate, maxFrames > 0 ? maxFrames : kIdleMaxFrames)) {
        return;
    }
    process(0, 0, {});
}

std::optional<vst::ParameterInfo> Vst3Plugin::Impl::info(vst::ParamID id) const {
    const Steinberg::int32 count = controller->getParameterCount();
    for (Steinberg::int32 index = 0; index < count; ++index) {
        vst::ParameterInfo found{};
        if (controller->getParameterInfo(index, found) == Steinberg::kResultOk && found.id == id) {
            return found;
        }
    }
    return std::nullopt;
}

// A stepped param counts its steps from 0, and any other param takes the plugin's plain value.
vst::ParamValue Vst3Plugin::Impl::normalized(vst::ParamID id, double value) const {
    const auto found = info(id);
    if (found && found->stepCount > 0) {
        return std::clamp(value / found->stepCount, 0.0, 1.0);
    }
    return std::clamp(controller->plainParamToNormalized(id, value), 0.0, 1.0);
}

PluginResult createVst3Plugin(const std::filesystem::path& bundle, const std::string& pluginId) {
    const auto classId = VST3::UID::fromString(pluginId, false);
    if (!classId) {
        return std::unexpected(pluginId + " is no VST3 class id");
    }
    auto module = openModule(bundle);
    if (!module) {
        return std::unexpected(module.error());
    }
    auto impl = std::make_unique<Vst3Plugin::Impl>();
    impl->module = *module;
    const VST3::Hosting::PluginFactory& factory = impl->module->getFactory();
    factory.setHostContext(&impl->host);
    const auto classes = factory.classInfos();
    const auto described = std::ranges::find(classes, *classId, &VST3::Hosting::ClassInfo::ID);
    if (described == classes.end()) {
        return std::unexpected(bundle.string() + " has no plugin " + pluginId);
    }
    impl->name = described->name();
    impl->component = factory.createInstance<vst::IComponent>(*classId);
    if (impl->component == nullptr || impl->component->initialize(&impl->host) != Steinberg::kResultOk) {
        return std::unexpected(pluginId + " failed to initialize");
    }
    impl->processor = Steinberg::U::cast<vst::IAudioProcessor>(impl->component);
    impl->controller = Steinberg::U::cast<vst::IEditController>(impl->component);
    if (impl->controller == nullptr) {
        Steinberg::TUID controllerId{};
        if (impl->component->getControllerClassId(std::data(controllerId)) == Steinberg::kResultOk) {
            impl->controller = factory.createInstance<vst::IEditController>(VST3::UID{controllerId});
        }
        impl->separateController = impl->controller != nullptr;
        if (impl->controller != nullptr && impl->controller->initialize(&impl->host) != Steinberg::kResultOk) {
            impl->controller = nullptr;
            impl->separateController = false;
        }
    }
    if (impl->processor == nullptr || impl->controller == nullptr) {
        impl->component->terminate();
        return std::unexpected(pluginId + " is no audio processor with a controller");
    }
    impl->controller->setComponentHandler(&impl->handler);
    if (impl->separateController) {
        impl->componentConnection = Steinberg::U::cast<vst::IConnectionPoint>(impl->component);
        impl->controllerConnection = Steinberg::U::cast<vst::IConnectionPoint>(impl->controller);
        if (impl->componentConnection != nullptr && impl->controllerConnection != nullptr) {
            impl->componentConnection->connect(impl->controllerConnection);
            impl->controllerConnection->connect(impl->componentConnection);
        }
    }
    Steinberg::MemoryStream state;
    if (impl->component->getState(&state) == Steinberg::kResultOk) {
        state.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        impl->controller->setComponentState(&state);
    }
    return std::make_shared<Vst3Plugin>(std::move(impl));
}

Vst3Plugin::Vst3Plugin(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Vst3Plugin::~Vst3Plugin() {
    Impl& impl = *impl_;
    {
        const AudioGuard guard(impl.audioGuard);
        impl.deactivate();
    }
    if (impl.componentConnection != nullptr && impl.controllerConnection != nullptr) {
        impl.componentConnection->disconnect(impl.controllerConnection);
        impl.controllerConnection->disconnect(impl.componentConnection);
    }
    impl.controller->setComponentHandler(nullptr);
    if (impl.separateController) {
        impl.controller->terminate();
    }
    impl.component->terminate();
    impl.componentConnection = nullptr;
    impl.controllerConnection = nullptr;
    impl.controller = nullptr;
    impl.processor = nullptr;
    impl.component = nullptr;
}

const std::string& Vst3Plugin::name() const { return impl_->name; }

bool Vst3Plugin::producesMidi() const { return false; }

bool Vst3Plugin::activate(double sampleRate, core::Frames maxFrames) {
    Impl& impl = *impl_;
    if (impl.active && impl.sampleRate == sampleRate && impl.maxFrames == maxFrames) {
        return true;
    }
    const AudioGuard guard(impl.audioGuard);
    return impl.activateAt(sampleRate, maxFrames);
}

core::Frames Vst3Plugin::latency() const { return impl_->active ? impl_->latency : 0; }

bool Vst3Plugin::idle() {
    Impl& impl = *impl_;
    if (impl.restartRequested.exchange(false, std::memory_order_relaxed) && impl.active) {
        const AudioGuard guard(impl.audioGuard);
        impl.activateAt(impl.sampleRate, impl.maxFrames);
    }
    return impl.stateDirty.exchange(false, std::memory_order_relaxed);
}

std::optional<std::vector<std::uint8_t>> Vst3Plugin::saveState() const {
    Steinberg::MemoryStream component;
    Steinberg::MemoryStream controller;
    if (impl_->component->getState(&component) != Steinberg::kResultOk) {
        return std::nullopt;
    }
    const bool controllerSaved =
        impl_->separateController && impl_->controller->getState(&controller) == Steinberg::kResultOk;
    std::vector<std::uint8_t> bytes(kStateTag.begin(), kStateTag.end());
    for (const auto part :
         {bytesOf(component), controllerSaved ? bytesOf(controller) : std::span<const std::uint8_t>{}}) {
        writeSize(bytes, static_cast<std::uint32_t>(part.size()));
        bytes.insert(bytes.end(), part.begin(), part.end());
    }
    return bytes;
}

bool Vst3Plugin::loadState(std::span<const std::uint8_t> bytes) {
    Impl& impl = *impl_;
    if (bytes.size() < kStateTag.size() || !std::ranges::equal(bytes.first(kStateTag.size()), kStateTag)) {
        return false;
    }
    std::span<const std::uint8_t> rest = bytes.subspan(kStateTag.size());
    const auto component = readPart(rest);
    const auto controller = readPart(rest);
    if (!component || !controller) {
        return false;
    }
    Steinberg::MemoryStream componentStream = readStream(*component);
    {
        const AudioGuard guard(impl.audioGuard);
        if (impl.component->setState(&componentStream) != Steinberg::kResultOk) {
            return false;
        }
    }
    componentStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    impl.controller->setComponentState(&componentStream);
    if (!controller->empty()) {
        Steinberg::MemoryStream controllerStream = readStream(*controller);
        impl.controller->setState(&controllerStream);
    }
    impl.paramsChanged.store(true, std::memory_order_relaxed);
    return true;
}

std::vector<PluginParam> Vst3Plugin::params() const {
    vst::IEditController& editable = *impl_->controller;
    const Steinberg::int32 count = editable.getParameterCount();
    std::vector<PluginParam> listed;
    for (Steinberg::int32 index = 0; index < count; ++index) {
        vst::ParameterInfo info{};
        if (editable.getParameterInfo(index, info) != Steinberg::kResultOk ||
            (info.flags & (vst::ParameterInfo::kIsHidden | vst::ParameterInfo::kIsReadOnly)) != 0) {
            continue;
        }
        const bool stepped = info.stepCount > 0;
        const double low = stepped ? 0.0 : editable.normalizedParamToPlain(info.id, 0.0);
        const double high = stepped ? info.stepCount : editable.normalizedParamToPlain(info.id, 1.0);
        PluginParam param{.id = info.id,
                          .name = vst::StringConvert::convert(std::data(info.title)),
                          .module = {},
                          .min = std::min(low, high),
                          .max = std::max(low, high),
                          .defaultValue = stepped
                                              ? std::round(info.defaultNormalizedValue * info.stepCount)
                                              : editable.normalizedParamToPlain(info.id, info.defaultNormalizedValue),
                          .stepped = stepped,
                          .enumerated = (info.flags & vst::ParameterInfo::kIsList) != 0,
                          .choices = {}};
        if (stepped && std::cmp_less(info.stepCount, kMaxParamChoices)) {
            param.choices = std::views::iota(0, info.stepCount + 1) |
                            std::views::transform([&](int step) { return paramText(info.id, step); }) |
                            std::ranges::to<std::vector>();
        }
        listed.push_back(std::move(param));
    }
    return listed;
}

std::optional<double> Vst3Plugin::paramValue(std::uint32_t id) const {
    const auto found = impl_->info(id);
    if (!found) {
        return std::nullopt;
    }
    const vst::ParamValue normalized = impl_->controller->getParamNormalized(id);
    if (found->stepCount > 0) {
        return std::round(normalized * found->stepCount);
    }
    return impl_->controller->normalizedParamToPlain(id, normalized);
}

std::string Vst3Plugin::paramText(std::uint32_t id, double value) const {
    vst::String128 text{};
    if (impl_->controller->getParamStringByValue(id, impl_->normalized(id, value), std::data(text)) !=
        Steinberg::kResultOk) {
        return std::format("{}", value);
    }
    return vst::StringConvert::convert(std::data(text));
}

void Vst3Plugin::setParam(std::uint32_t id, double value) {
    Impl& impl = *impl_;
    const vst::ParamValue normalized = impl.normalized(id, value);
    impl.controller->setParamNormalized(id, normalized);
    // We wait for every change before returning, so the queue holds at most this one.
    if (!writeRecord(impl.paramQueue, Vst3ParamChange{.id = id, .value = normalized})) {
        return;
    }
    ++impl.paramsSent;
    const auto deadline = std::chrono::steady_clock::now() + kParamWait;
    while (impl.paramsApplied.load(std::memory_order_acquire) < impl.paramsSent) {
        if (!impl.active || std::chrono::steady_clock::now() > deadline) {
            const AudioGuard guard(impl.audioGuard);
            impl.flushParams();
            return;
        }
        std::this_thread::yield();
    }
}

bool Vst3Plugin::takeParamsChanged() { return impl_->paramsChanged.exchange(false, std::memory_order_relaxed); }

double Vst3Plugin::processValue(std::uint32_t id, double value) const { return impl_->normalized(id, value); }

bool Vst3Plugin::hasEditor() const { return false; }

bool Vst3Plugin::editorOpen() const { return false; }

bool Vst3Plugin::openFloatingEditor(const std::string& /*title*/) { return false; }

std::optional<EditorSize> Vst3Plugin::embedEditor(void* /*parent*/) { return std::nullopt; }

void Vst3Plugin::closeEditor() {}

std::optional<EditorSize> Vst3Plugin::takeEditorResize() { return std::nullopt; }

}  // namespace emerick::engine
