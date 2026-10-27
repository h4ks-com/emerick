// The main-thread side of a CLAP instrument: creation, activation, state, the editor and destruction.

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

bool prefersClapNotes(const ClapPluginProxy& proxy) {
    if (!proxy.canUseNotePorts() || proxy.notePortsCount(true) == 0) {
        return true;
    }
    clap_note_port_info info{};
    return !proxy.notePortsGet(0, true, &info) || (info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0;
}

}  // namespace

AudioGuard::AudioGuard(std::atomic<bool>& guard) : guard_(guard) {
    while (guard_.exchange(true, std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

AudioGuard::~AudioGuard() { guard_.store(false, std::memory_order_release); }

AudioThreadScope::AudioThreadScope() { audioThreadFlag() = true; }

AudioThreadScope::~AudioThreadScope() { audioThreadFlag() = false; }

bool AudioThreadScope::active() { return audioThreadFlag(); }

ClapInstrument::Impl::Impl() : ClapHostBase("Emerick", "Emerick", "", "1") {}

bool ClapInstrument::Impl::threadCheckIsMainThread() const noexcept {
    return std::this_thread::get_id() == mainThread && !AudioThreadScope::active();
}

bool ClapInstrument::Impl::threadCheckIsAudioThread() const noexcept { return AudioThreadScope::active(); }

bool ClapInstrument::Impl::guiRequestResize(std::uint32_t width, std::uint32_t height) noexcept {
    requestedWidth.store(width, std::memory_order_relaxed);
    requestedHeight.store(height, std::memory_order_relaxed);
    resizeRequested.store(true, std::memory_order_release);
    return true;
}

bool ClapInstrument::Impl::timerSupportRegisterTimer(std::uint32_t periodMs, clap_id* timerId) noexcept {
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

bool ClapInstrument::Impl::timerSupportUnregisterTimer(clap_id timerId) noexcept {
    return std::erase_if(timers, [timerId](const PluginTimer& timer) { return timer.id == timerId; }) > 0;
}

void ClapInstrument::Impl::fireDueTimers() {
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

void ClapInstrument::Impl::guiClosed(bool wasDestroyed) noexcept {
    editorClosedByPlugin = !wasDestroyed;
    editorOpen = false;
}

void ClapInstrument::Impl::deactivate() {
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

void ClapInstrument::Impl::allocateBuffers(core::Frames frames) {
    allocatePorts(portChannels(*proxy, true), frames, inputPorts, inputBuffers);
    allocatePorts(portChannels(*proxy, false), frames, outputPorts, outputBuffers);
}

std::expected<std::shared_ptr<ClapInstrument>, std::string> ClapInstrument::create(const std::filesystem::path& bundle,
                                                                                   const std::string& pluginId) {
    auto library = ClapLibrary::open(bundle);
    if (!library) {
        return std::unexpected(library.error());
    }
    auto impl = std::make_unique<Impl>();
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
    return std::make_shared<ClapInstrument>(std::move(impl));
}

ClapInstrument::ClapInstrument(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ClapInstrument::~ClapInstrument() {
    closeEditor();
    {
        const AudioGuard guard(impl_->audioGuard);
        impl_->deactivate();
    }
    impl_->proxy->destroy();
}

const std::string& ClapInstrument::name() const { return impl_->name; }

bool ClapInstrument::activate(double sampleRate, core::Frames maxFrames) {
    Impl& impl = *impl_;
    if (impl.active && impl.sampleRate == sampleRate && impl.maxFrames == maxFrames) {
        return true;
    }
    const AudioGuard guard(impl.audioGuard);
    impl.deactivate();
    impl.allocateBuffers(maxFrames);
    impl.clapNotes = prefersClapNotes(*impl.proxy);
    impl.sampleRate = sampleRate;
    impl.maxFrames = maxFrames;
    impl.active = impl.proxy->activate(sampleRate, 1, static_cast<std::uint32_t>(maxFrames));
    return impl.active;
}

bool ClapInstrument::idle() {
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

std::optional<std::vector<std::uint8_t>> ClapInstrument::saveState() const {
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

bool ClapInstrument::loadState(std::span<const std::uint8_t> bytes) {
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

bool ClapInstrument::hasEditor() const { return impl_->proxy->canUseGui(); }

bool ClapInstrument::editorOpen() const { return impl_->editorOpen; }

bool ClapInstrument::openFloatingEditor(const std::string& title) {
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

std::optional<EditorSize> ClapInstrument::embedEditor(void* parent) {
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

void ClapInstrument::closeEditor() {
    if (std::exchange(impl_->editorOpen, false)) {
        impl_->proxy->guiDestroy();
    }
}

std::optional<EditorSize> ClapInstrument::takeEditorResize() {
    if (!impl_->resizeRequested.exchange(false, std::memory_order_acquire)) {
        return std::nullopt;
    }
    return EditorSize{.width = impl_->requestedWidth.load(std::memory_order_relaxed),
                      .height = impl_->requestedHeight.load(std::memory_order_relaxed)};
}

}  // namespace emerick::engine
