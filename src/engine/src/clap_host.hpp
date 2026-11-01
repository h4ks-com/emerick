#pragma once

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <clap/helpers/host.hh>
#include <clap/helpers/plugin-proxy.hh>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "audio_guard.hpp"
#include "clap_library.hpp"
#include "clap_plugin.hpp"
#include "spsc_ring.hpp"

namespace emerick::engine {

inline constexpr auto kClapMisbehaviour = clap::helpers::MisbehaviourHandler::Ignore;
inline constexpr auto kClapChecking = clap::helpers::CheckingLevel::None;
using ClapHostBase = clap::helpers::Host<kClapMisbehaviour, kClapChecking>;
using ClapPluginProxy = clap::helpers::PluginProxy<kClapMisbehaviour, kClapChecking>;

inline constexpr std::size_t kMaxQueuedMidi = 1024;
inline constexpr std::size_t kMaxQueuedParams = 64;
// Every queued or produced message keeps its bytes in a pool this large per block, so a sysex needs no allocation.
inline constexpr std::size_t kMidiPoolBytes = std::size_t{1} << 16;

struct QueuedMidi {
    std::int64_t timestampNs = 0;
    std::size_t offset = 0;
    std::size_t size = 0;
};

// Fixed buffers for a block's MIDI messages and their bytes, so the audio thread never allocates for them.
template <class Message>
struct MidiPool {
    std::array<Message, kMaxQueuedMidi> messages{};
    std::size_t count = 0;
    std::array<std::uint8_t, kMidiPoolBytes> bytes{};
    std::size_t used = 0;

    // Copies bytes into the pool; nullopt when the messages or the bytes are full.
    std::optional<std::span<const std::uint8_t>> store(std::span<const std::uint8_t> message) {
        if (count == messages.size() || message.size() > bytes.size() - used) {
            return std::nullopt;
        }
        const auto stored = std::span(bytes).subspan(used, message.size());
        std::ranges::copy(message, stored.begin());
        used += message.size();
        return stored;
    }

    void clear() {
        count = 0;
        used = 0;
    }
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
struct ClapPlugin::Impl final : ClapHostBase {
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
    [[nodiscard]] bool implementsLatency() const noexcept override { return true; }

    // Main thread: idle calls this as often as it runs, so a timer fires at most once per idle.
    void fireDueTimers();

    // Main thread, holding the audio guard: stops processing as the audio thread, then deactivates.
    void deactivate();
    void allocateBuffers(core::Frames maxFrames);
    // Holding the audio guard: moves the queued param changes into the event list, after any MIDI already there.
    void collectParams();
    // Audio thread: moves the automated param values into the event list.
    void collectAutomation();
    // Holding the audio guard as the audio thread: runs one process call into the output ports; false when the plugin
    // is not processing or fails.
    bool process(core::Frames frames, std::int64_t hostTimeNs, const InstrumentTransport& transport);
    // Main thread, holding the audio guard: hands the queued param changes to the plugin's flush.
    void flushParams();
    // The event list holds the collected events, and the output list marks params changed by the plugin itself.
    clap_input_events inputEvents();
    clap_output_events outputEvents();
    // Audio thread: also collects the notes, MIDI and sysex the plugin sends into produced.
    clap_output_events processOutputEvents();

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
    bool takesSysex = false;
    std::vector<PortBuffers> inputPorts;
    std::vector<PortBuffers> outputPorts;
    std::vector<clap_audio_buffer> inputBuffers;
    std::vector<clap_audio_buffer> outputBuffers;
    MidiPool<QueuedMidi> queued;
    std::array<clap_event_note, kMaxQueuedMidi> notes{};
    std::array<clap_event_midi, kMaxQueuedMidi> midi{};
    std::array<clap_event_midi_sysex, kMaxQueuedMidi> sysex{};
    std::array<clap_event_param_value, kMaxQueuedParams> paramEvents{};
    std::size_t paramEventCount = 0;
    std::array<QueuedParam, kMaxQueuedParams> automated{};
    std::size_t automatedCount = 0;
    std::array<clap_event_param_value, kMaxQueuedParams> automatedEvents{};
    std::array<const clap_event_header*, kMaxQueuedMidi + (2 * kMaxQueuedParams)> events{};
    std::size_t eventCount = 0;

    // Only the audio thread touches these, in and after processBlock.
    MidiPool<InstrumentMidi> produced;

    // Main thread only.
    bool editorOpen = false;
    bool editorClosedByPlugin = false;
    std::vector<PluginTimer> timers;
    clap_id nextTimerId = 1;
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
