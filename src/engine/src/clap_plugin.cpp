// The main-thread side of a CLAP plugin: creation, activation, state, the editor and destruction.

// clap-helpers' host.hxx calls std::terminate without including <exception>, so we include it first.
// clang-format off
#include <exception>
#include <clap/helpers/host.hxx>
#include <clap/helpers/plugin-proxy.hxx>
// clang-format on

#include <algorithm>
#include <cstring>
#include <new>
#include <ranges>
#include <stdexcept>
#include <utility>

#include "clap_host.hpp"

// clap-helpers declares the host's destructor protected and virtual, for its subclasses to delete through.
// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
template class clap::helpers::Host<emerick::engine::kClapMisbehaviour, emerick::engine::kClapChecking>;
template class clap::helpers::PluginProxy<emerick::engine::kClapMisbehaviour, emerick::engine::kClapChecking>;

namespace emerick::engine {

namespace {

constexpr std::uint32_t kDefaultOutputChannels = 2;

#ifdef __APPLE__
constexpr const char* kEditorApi = std::data(CLAP_WINDOW_API_COCOA);
#elifdef _WIN32
constexpr const char* kEditorApi = std::data(CLAP_WINDOW_API_WIN32);
#else
constexpr const char* kEditorApi = std::data(CLAP_WINDOW_API_X11);
#endif

bool& audioThreadFlag() {
    thread_local bool flag = false;
    return flag;
}

std::vector<std::uint32_t> portChannels(const ClapPluginProxy& proxy, bool input) {
    if (!proxy.canUseAudioPorts()) {
        return input ? std::vector<std::uint32_t>{} : std::vector{kDefaultOutputChannels};
    }
    std::vector<std::uint32_t> channels;
    for (std::uint32_t index = 0; index < proxy.audioPortsCount(input); ++index) {
        clap_audio_port_info info{};
        channels.push_back(proxy.audioPortsGet(index, input, &info) ? info.channel_count : 0);
    }
    return channels;
}

// Every buffer stays allocated while the plugin is active, so the audio thread never allocates.
void allocatePorts(const std::vector<std::uint32_t>& channels, core::Frames maxFrames, std::vector<PortBuffers>& ports,
                   std::vector<clap_audio_buffer>& buffers) {
    ports.assign(channels.size(), {});
    buffers.assign(channels.size(), {});
    for (std::size_t index = 0; index < channels.size(); ++index) {
        PortBuffers& port = ports[index];
        port.channels.assign(channels[index], std::vector<float>(static_cast<std::size_t>(maxFrames)));
        port.pointers = port.channels |
                        std::views::transform([](std::vector<float>& channel) { return channel.data(); }) |
                        std::ranges::to<std::vector>();
        buffers[index] = {.data32 = port.pointers.data(),
                          .data64 = nullptr,
                          .channel_count = channels[index],
                          .latency = 0,
                          .constant_mask = 0};
    }
}

// The dialects of the plugin's first note input; a plugin without note ports reads CLAP notes.
std::uint32_t noteDialects(const ClapPluginProxy& proxy) {
    clap_note_port_info info{};
    if (!proxy.canUseNotePorts() || proxy.notePortsCount(true) == 0 || !proxy.notePortsGet(0, true, &info)) {
        return CLAP_NOTE_DIALECT_CLAP;
    }
    return info.supported_dialects;
}

}  // namespace

AudioThreadScope::AudioThreadScope() { audioThreadFlag() = true; }

AudioThreadScope::~AudioThreadScope() { audioThreadFlag() = false; }

bool AudioThreadScope::active() { return audioThreadFlag(); }

ClapPlugin::Impl::Impl() : ClapHostBase("Emerick", "Emerick", "", "1") {}

bool ClapPlugin::Impl::threadCheckIsMainThread() const noexcept {
    return std::this_thread::get_id() == mainThread && !AudioThreadScope::active();
}

bool ClapPlugin::Impl::threadCheckIsAudioThread() const noexcept { return AudioThreadScope::active(); }

bool ClapPlugin::Impl::guiRequestResize(std::uint32_t width, std::uint32_t height) noexcept {
    requestedWidth.store(width, std::memory_order_relaxed);
    requestedHeight.store(height, std::memory_order_relaxed);
    resizeRequested.store(true, std::memory_order_release);
    return true;
}

bool ClapPlugin::Impl::timerSupportRegisterTimer(std::uint32_t periodMs, clap_id* timerId) noexcept {
    if (!threadCheckIsMainThread()) {
        return false;
    }
    const auto period = std::chrono::milliseconds{periodMs};
    try {
        timers.push_back({.id = nextTimerId++, .period = period, .due = std::chrono::steady_clock::now() + period});
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    *timerId = timers.back().id;
    return true;
}

bool ClapPlugin::Impl::timerSupportUnregisterTimer(clap_id timerId) noexcept {
    return std::erase_if(timers, [timerId](const PluginTimer& timer) { return timer.id == timerId; }) > 0;
}

void ClapPlugin::Impl::fireDueTimers() {
    const auto now = std::chrono::steady_clock::now();
    // A timer callback can unregister timers, so we collect the due ids before calling any.
    std::vector<clap_id> due;
    for (PluginTimer& timer : timers) {
        if (timer.due <= now) {
            due.push_back(timer.id);
            timer.due = now + timer.period;
        }
    }
    for (const clap_id id : due) {
        proxy->timerSupportOnTimer(id);
    }
}

void ClapPlugin::Impl::guiClosed(bool wasDestroyed) noexcept {
    editorClosedByPlugin = !wasDestroyed;
    editorOpen = false;
}

void ClapPlugin::Impl::deactivate() {
    if (processing.load(std::memory_order_relaxed)) {
        const AudioThreadScope audioThread;
        proxy->stopProcessing();
        processing.store(false, std::memory_order_relaxed);
    }
    if (active) {
        proxy->deactivate();
        active = false;
    }
}

void ClapPlugin::Impl::allocateBuffers(core::Frames frames) {
    allocatePorts(portChannels(*proxy, true), frames, inputPorts, inputBuffers);
    allocatePorts(portChannels(*proxy, false), frames, outputPorts, outputBuffers);
}

PluginResult createClapPlugin(const std::filesystem::path& bundle, const std::string& pluginId) {
    auto library = ClapLibrary::open(bundle);
    if (!library) {
        return std::unexpected(library.error());
    }
    auto impl = std::make_unique<ClapPlugin::Impl>();
    impl->library = *library;
    const clap_plugin_factory& factory = impl->library->factory();
    const clap_plugin* plugin = factory.create_plugin(&factory, impl->clapHost(), pluginId.c_str());
    if (plugin == nullptr) {
        return std::unexpected(bundle.string() + " has no plugin " + pluginId);
    }
    impl->proxy = std::make_unique<ClapPluginProxy>(*plugin, *impl);
    if (!impl->proxy->init()) {
        impl->proxy->destroy();
        return std::unexpected(pluginId + " failed to initialize");
    }
    impl->name = plugin->desc != nullptr && plugin->desc->name != nullptr ? plugin->desc->name : pluginId;
    return std::make_shared<ClapPlugin>(std::move(impl));
}

ClapPlugin::ClapPlugin(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ClapPlugin::~ClapPlugin() {
    closeEditor();
    {
        const AudioGuard guard(impl_->audioGuard);
        impl_->deactivate();
    }
    impl_->proxy->destroy();
}

const std::string& ClapPlugin::name() const { return impl_->name; }

bool ClapPlugin::producesMidi() const {
    return impl_->proxy->canUseNotePorts() && impl_->proxy->notePortsCount(false) > 0;
}

bool ClapPlugin::activate(double sampleRate, core::Frames maxFrames) {
    Impl& impl = *impl_;
    if (impl.active && impl.sampleRate == sampleRate && impl.maxFrames == maxFrames) {
        return true;
    }
    const AudioGuard guard(impl.audioGuard);
    impl.deactivate();
    impl.allocateBuffers(maxFrames);
    const std::uint32_t dialects = noteDialects(*impl.proxy);
    impl.clapNotes = (dialects & CLAP_NOTE_DIALECT_CLAP) != 0;
    impl.takesSysex = (dialects & CLAP_NOTE_DIALECT_MIDI) != 0;
    impl.sampleRate = sampleRate;
    impl.maxFrames = maxFrames;
    impl.active = impl.proxy->activate(sampleRate, 1, static_cast<std::uint32_t>(maxFrames));
    return impl.active;
}

core::Frames ClapPlugin::latency() const {
    return impl_->active && impl_->proxy->canUseLatency() ? impl_->proxy->latencyGet() : 0;
}

bool ClapPlugin::idle() {
    Impl& impl = *impl_;
    if (impl.callbackRequested.exchange(false, std::memory_order_relaxed)) {
        impl.proxy->onMainThread();
    }
    if (impl.restartRequested.exchange(false, std::memory_order_relaxed) && impl.active) {
        const AudioGuard guard(impl.audioGuard);
        impl.deactivate();
        impl.active = impl.proxy->activate(impl.sampleRate, 1, static_cast<std::uint32_t>(impl.maxFrames));
    }
    // A processing plugin flushes in its next process call.
    if (impl.flushRequested.exchange(false, std::memory_order_relaxed) &&
        !impl.processing.load(std::memory_order_relaxed)) {
        const AudioGuard guard(impl.audioGuard);
        impl.flushParams();
    }
    if (std::exchange(impl.editorClosedByPlugin, false)) {
        impl.proxy->guiDestroy();
    }
    if (impl.proxy->canUseTimerSupport()) {
        impl.fireDueTimers();
    }
    return impl.stateDirty.exchange(false, std::memory_order_relaxed);
}

void ClapPlugin::setOfflineRendering(bool offline) {
    if (impl_->proxy->canUseRender()) {
        impl_->proxy->renderSet(offline ? CLAP_RENDER_OFFLINE : CLAP_RENDER_REALTIME);
    }
}

std::optional<std::vector<std::uint8_t>> ClapPlugin::saveState() const {
    if (!impl_->proxy->canUseState()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes;
    const clap_ostream stream{.ctx = &bytes,
                              .write = [](const clap_ostream* out, const void* data, std::uint64_t size) {
                                  auto& written = *static_cast<std::vector<std::uint8_t>*>(out->ctx);
                                  const auto* first = static_cast<const std::uint8_t*>(data);
                                  written.insert(written.end(), first, first + size);
                                  return static_cast<std::int64_t>(size);
                              }};
    return impl_->proxy->stateSave(&stream) ? std::optional{std::move(bytes)} : std::nullopt;
}

bool ClapPlugin::loadState(std::span<const std::uint8_t> bytes) {
    if (!impl_->proxy->canUseState()) {
        return false;
    }
    struct Reader {
        std::span<const std::uint8_t> left;
    } reader{bytes};
    const clap_istream stream{.ctx = &reader, .read = [](const clap_istream* in, void* data, std::uint64_t size) {
                                  auto& source = *static_cast<Reader*>(in->ctx);
                                  const std::size_t count = std::min<std::size_t>(size, source.left.size());
                                  std::memcpy(data, source.left.data(), count);
                                  source.left = source.left.subspan(count);
                                  return static_cast<std::int64_t>(count);
                              }};
    return impl_->proxy->stateLoad(&stream);
}

bool ClapPlugin::hasEditor() const { return impl_->proxy->canUseGui(); }

bool ClapPlugin::editorOpen() const { return impl_->editorOpen; }

bool ClapPlugin::openFloatingEditor(const std::string& title) {
    ClapPluginProxy& proxy = *impl_->proxy;
    // Some plugins never report that the user closed their window, and showing it again then shows nothing, so we
    // always create it anew.
    closeEditor();
    if (!proxy.canUseGui() || !proxy.guiIsApiSupported(kEditorApi, true) || !proxy.guiCreate(kEditorApi, true)) {
        return false;
    }
    proxy.guiSuggestTitle(title.c_str());
    impl_->editorOpen = proxy.guiShow();
    if (!impl_->editorOpen) {
        proxy.guiDestroy();
    }
    return impl_->editorOpen;
}

std::optional<EditorSize> ClapPlugin::embedEditor(void* parent) {
    ClapPluginProxy& proxy = *impl_->proxy;
    if (!proxy.canUseGui() || !proxy.guiIsApiSupported(kEditorApi, false) || !proxy.guiCreate(kEditorApi, false)) {
        return std::nullopt;
    }
    clap_window window{};
    window.api = kEditorApi;
    // clap_window keeps the handle in a union, and the platform decides which member names it.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
#ifdef __APPLE__
    window.cocoa = parent;
#elifdef _WIN32
    window.win32 = parent;
#else
    window.x11 = reinterpret_cast<std::uintptr_t>(parent);
#endif
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    EditorSize size;
    if (!proxy.guiSetParent(&window) || !proxy.guiGetSize(&size.width, &size.height) || !proxy.guiShow()) {
        proxy.guiDestroy();
        return std::nullopt;
    }
    impl_->editorOpen = true;
    return size;
}

void ClapPlugin::closeEditor() {
    if (std::exchange(impl_->editorOpen, false)) {
        impl_->proxy->guiDestroy();
    }
}

bool ClapPlugin::editorResizable() const { return impl_->editorOpen && impl_->proxy->guiCanResize(); }

EditorSize ClapPlugin::resizeEditor(EditorSize size) {
    ClapPluginProxy& proxy = *impl_->proxy;
    if (!editorResizable() || !proxy.guiAdjustSize(&size.width, &size.height) ||
        !proxy.guiSetSize(size.width, size.height)) {
        EditorSize current;
        return proxy.guiGetSize(&current.width, &current.height) ? current : size;
    }
    return size;
}

std::optional<EditorSize> ClapPlugin::takeEditorResize() {
    if (!impl_->resizeRequested.exchange(false, std::memory_order_acquire)) {
        return std::nullopt;
    }
    return EditorSize{.width = impl_->requestedWidth.load(std::memory_order_relaxed),
                      .height = impl_->requestedHeight.load(std::memory_order_relaxed)};
}

}  // namespace emerick::engine
