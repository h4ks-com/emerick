#pragma once

#include <clap/clap.h>

#include <array>
#include <atomic>
#include <chrono>
#include <clap/helpers/host.hh>
#include <clap/helpers/plugin-proxy.hh>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "clap_library.hpp"
#include "emerick/engine/clap_instrument.hpp"
#include "spsc_ring.hpp"

namespace emerick::engine {

inline constexpr auto kClapMisbehaviour = clap::helpers::MisbehaviourHandler::Ignore;
inline constexpr auto kClapChecking = clap::helpers::CheckingLevel::None;
using ClapHostBase = clap::helpers::Host<kClapMisbehaviour, kClapChecking>;
using ClapPluginProxy = clap::helpers::PluginProxy<kClapMisbehaviour, kClapChecking>;

inline constexpr std::size_t kMaxQueuedMidi = 1024;
inline constexpr std::size_t kMaxQueuedParams = 64;

struct QueuedMidi {
    std::int64_t timestampNs = 0;
    std::array<std::uint8_t, 3> bytes{};
    std::uint8_t size = 0;
};

struct QueuedParam {
    clap_id id = 0;
    double value = 0.0;
};

struct PluginTimer {
    clap_id id = 0;
    std::chrono::milliseconds period{};
    std::chrono::steady_clock::time_point due;
};

struct PortBuffers {
    std::vector<std::vector<float>> channels;
    std::vector<float*> pointers;
};

// The host side of one plugin instance. The plugin calls back into it from any thread, so a callback that is not
// realtime safe only sets a flag that idle reads on the main thread.
struct ClapInstrument::Impl final : ClapHostBase {
    Impl();
    ~Impl() override = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    [[nodiscard]] bool threadCheckIsMainThread() const noexcept override;
    [[nodiscard]] bool threadCheckIsAudioThread() const noexcept override;
    void requestRestart() noexcept override { restartRequested.store(true, std::memory_order_relaxed); }
    void requestProcess() noexcept override {}
    void requestCallback() noexcept override { callbackRequested.store(true, std::memory_order_relaxed); }
    [[nodiscard]] bool implementsState() const noexcept override { return true; }
    void stateMarkDirty() noexcept override { stateDirty.store(true, std::memory_order_relaxed); }
    [[nodiscard]] bool implementsGui() const noexcept override { return true; }
    bool guiRequestResize(std::uint32_t width, std::uint32_t height) noexcept override;
    void guiClosed(bool wasDestroyed) noexcept override;
    [[nodiscard]] bool implementsTimerSupport() const noexcept override { return true; }
    bool timerSupportRegisterTimer(std::uint32_t periodMs, clap_id* timerId) noexcept override;
    bool timerSupportUnregisterTimer(clap_id timerId) noexcept override;
    [[nodiscard]] bool implementsParams() const noexcept override { return true; }
    void paramsRescan(clap_param_rescan_flags /*flags*/) noexcept override {
        paramsChanged.store(true, std::memory_order_relaxed);
    }
    void paramsRequestFlush() noexcept override { flushRequested.store(true, std::memory_order_relaxed); }

    // Main thread: idle calls this as often as it runs, so a timer fires at most once per idle.
    void fireDueTimers();

    // Main thread, holding the audio guard: stops processing as the audio thread, then deactivates.
    void deactivate();
    void allocateBuffers(core::Frames maxFrames);
    // Holding the audio guard: moves the queued param changes into the event list, after any MIDI already there.
    void collectParams();
    // Main thread, holding the audio guard: hands the queued param changes to the plugin's flush.
    void flushParams();
    // The event list holds the collected events, and the output list marks params changed by the plugin itself.
    clap_input_events inputEvents();
    clap_output_events outputEvents();

    std::thread::id mainThread = std::this_thread::get_id();
    std::shared_ptr<ClapLibrary> library;
    std::unique_ptr<ClapPluginProxy> proxy;
    std::string name;

    std::atomic<bool> audioGuard{false};
    std::atomic<bool> restartRequested{false};
    std::atomic<bool> callbackRequested{false};
    std::atomic<bool> stateDirty{false};
    std::atomic<bool> resizeRequested{false};
    std::atomic<std::uint32_t> requestedWidth{0};
    std::atomic<std::uint32_t> requestedHeight{0};
    std::atomic<bool> paramsChanged{false};
    std::atomic<bool> flushRequested{false};
    // The main thread writes param changes and the holder of the audio guard reads them; each count grows by one per
    // change, so the main thread knows when the plugin has every change it sent.
    SpscRing paramQueue{kMaxQueuedParams * sizeof(QueuedParam)};
    std::uint64_t paramsSent = 0;
    std::atomic<std::uint64_t> paramsApplied{0};

    // The holder of the audio guard owns these. The main thread may read processing without the guard as a hint.
    bool active = false;
    std::atomic<bool> processing{false};
    double sampleRate = 0.0;
    core::Frames maxFrames = 0;
    bool clapNotes = true;
    std::vector<PortBuffers> inputPorts;
    std::vector<PortBuffers> outputPorts;
    std::vector<clap_audio_buffer> inputBuffers;
    std::vector<clap_audio_buffer> outputBuffers;
    std::array<QueuedMidi, kMaxQueuedMidi> queued{};
    std::size_t queuedCount = 0;
    std::array<clap_event_note, kMaxQueuedMidi> notes{};
    std::array<clap_event_midi, kMaxQueuedMidi> midi{};
    std::array<clap_event_param_value, kMaxQueuedParams> paramEvents{};
    std::size_t paramEventCount = 0;
    std::array<const clap_event_header*, kMaxQueuedMidi + kMaxQueuedParams> events{};
    std::size_t eventCount = 0;

    // Main thread only.
    bool editorOpen = false;
    bool editorClosedByPlugin = false;
    std::vector<PluginTimer> timers;
    clap_id nextTimerId = 1;
};

// Takes the audio guard for the main thread, waiting out the block the audio thread may be processing.
class AudioGuard {
public:
    explicit AudioGuard(std::atomic<bool>& guard);
    ~AudioGuard();
    AudioGuard(const AudioGuard&) = delete;
    AudioGuard& operator=(const AudioGuard&) = delete;
    AudioGuard(AudioGuard&&) = delete;
    AudioGuard& operator=(AudioGuard&&) = delete;

private:
    std::atomic<bool>& guard_;
};

// Marks the current thread as the plugin's audio thread while it holds the audio guard.
class AudioThreadScope {
public:
    AudioThreadScope();
    ~AudioThreadScope();
    AudioThreadScope(const AudioThreadScope&) = delete;
    AudioThreadScope& operator=(const AudioThreadScope&) = delete;
    AudioThreadScope(AudioThreadScope&&) = delete;
    AudioThreadScope& operator=(AudioThreadScope&&) = delete;

    static bool active();
};

}  // namespace emerick::engine
