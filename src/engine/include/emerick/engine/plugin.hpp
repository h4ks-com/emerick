#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "emerick/core/plugin_format.hpp"
#include "emerick/core/time.hpp"

namespace emerick::engine {

using core::PluginFormat;

struct EditorSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct PluginParam {
    std::uint32_t id = 0;
    std::string name;
    std::string module;
    double min = 0.0;
    double max = 0.0;
    double defaultValue = 0.0;
    bool stepped = false;
    bool enumerated = false;
    // The plugin's label for each step from min to max, for a stepped param with up to kMaxParamChoices steps.
    std::vector<std::string> choices;
};

inline constexpr std::size_t kMaxParamChoices = 8192;

// A message the plugin sent on its note outputs, offset frames into the block it processed.
struct InstrumentMidi {
    core::Frames offset = 0;
    std::span<const std::uint8_t> bytes;
};

// Where the host transport stands at the start of a block, in quarter notes from the project start.
struct InstrumentTransport {
    bool playing = false;
    double beats = 0.0;
    double bpm = 120.0;
    core::Meter meter{};
    // Set while playback jumps from loopEndBeats back to loopStartBeats.
    bool looping = false;
    double loopStartBeats = 0.0;
    double loopEndBeats = 0.0;
};

// One plugin instance, playing as a track's instrument or as an insert effect. The thread that creates it is the
// plugin's main thread: it activates, saves, loads, runs the editor and destroys it. The audio thread queues MIDI and
// params and processes. Whoever holds the plugin's audio guard is its audio thread, so the main thread can stop
// processing and reactivate the plugin while the audio thread skips its blocks, and neither ever waits on a lock the
// other holds for long.
class Plugin {
public:
    Plugin() = default;
    virtual ~Plugin() = default;
    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;
    Plugin(Plugin&&) = delete;
    Plugin& operator=(Plugin&&) = delete;

    [[nodiscard]] virtual const std::string& name() const = 0;
    // True when the plugin has a note output, so other tracks can take MIDI from it.
    [[nodiscard]] virtual bool producesMidi() const = 0;
    // Activates the plugin at this rate and largest block, reactivating it when either changed; false when it refuses.
    virtual bool activate(double sampleRate, core::Frames maxFrames) = 0;
    // The frames the plugin delays its audio by while active.
    [[nodiscard]] virtual core::Frames latency() const = 0;
    // Runs what the plugin asked of the main thread: its callback, a restart and closing its editor. Returns true once
    // after the plugin marked its state changed.
    virtual bool idle() = 0;
    // Nullopt when the plugin keeps no state or fails to save it.
    [[nodiscard]] virtual std::optional<std::vector<std::uint8_t>> saveState() const = 0;
    virtual bool loadState(std::span<const std::uint8_t> bytes) = 0;

    // The params the plugin shows, leaving out hidden and read-only ones.
    [[nodiscard]] virtual std::vector<PluginParam> params() const = 0;
    [[nodiscard]] virtual std::optional<double> paramValue(std::uint32_t id) const = 0;
    [[nodiscard]] virtual std::string paramText(std::uint32_t id, double value) const = 0;
    // Main thread: queues the value for the next process call, or flushes it while the plugin is not processing, and
    // returns once the plugin has it, so a state saved next holds it.
    virtual void setParam(std::uint32_t id, double value) = 0;
    // True once after the plugin asked to rescan its params or reported a param change of its own.
    virtual bool takeParamsChanged() = 0;
    // Main thread: the param value as queueParam takes it.
    [[nodiscard]] virtual double processValue(std::uint32_t id, double value) const = 0;

    [[nodiscard]] virtual bool hasEditor() const = 0;
    [[nodiscard]] virtual bool editorOpen() const = 0;
    // Opens the plugin's own editor window when it has one; false when it only embeds.
    virtual bool openFloatingEditor(const std::string& title) = 0;
    // Embeds the editor in parent, the platform's native view (NSView, X11 window or HWND), and returns its size.
    virtual std::optional<EditorSize> embedEditor(void* parent) = 0;
    virtual void closeEditor() = 0;
    // The size the embedded editor asked for since the last call.
    virtual std::optional<EditorSize> takeEditorResize() = 0;

    // Realtime: queues a MIDI message or a sysex for the next processBlock, which plays it at its timestamp in that
    // block. A sysex reaches only a plugin whose note input takes MIDI.
    virtual void queueMidi(std::span<const std::uint8_t> bytes, std::int64_t timestampNs) = 0;
    // Realtime: queues a param value from processValue for the start of the next process call.
    virtual void queueParam(std::uint32_t id, double value) = 0;
    // Realtime: adds the plugin's main output to each mix channel; a mix channel past its outputs takes the last one.
    virtual void processBlock(std::span<float* const> mix, core::Frames frames, std::int64_t hostTimeNs,
                              const InstrumentTransport& transport) = 0;
    // Realtime: plays the channels through the plugin's main input and replaces them with its main output. A channel
    // past its inputs or outputs takes the last one.
    virtual void processEffect(std::span<float* const> channels, core::Frames frames, std::int64_t hostTimeNs,
                               const InstrumentTransport& transport) = 0;
    // Realtime: the notes, MIDI and sysex the plugin sent in the last processBlock, valid until the next one.
    [[nodiscard]] virtual std::span<const InstrumentMidi> producedMidi() const = 0;
};

using PluginResult = std::expected<std::shared_ptr<Plugin>, std::string>;

PluginResult createClapPlugin(const std::filesystem::path& bundle, const std::string& pluginId);
// The plugin id of a VST3 plugin is its class id as 32 hex digits.
PluginResult createVst3Plugin(const std::filesystem::path& bundle, const std::string& pluginId);
PluginResult createPlugin(PluginFormat format, const std::filesystem::path& bundle, const std::string& pluginId);

}  // namespace emerick::engine
