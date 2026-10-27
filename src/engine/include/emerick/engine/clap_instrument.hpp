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

#include "emerick/core/time.hpp"

namespace emerick::engine {

struct EditorSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct ClapParam {
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

inline constexpr std::size_t kMaxParamChoices = 1024;

// One CLAP plugin playing as a track's instrument. The thread that creates it is the plugin's main thread: it
// activates, saves, loads, runs the editor and destroys it. The audio thread queues MIDI and processes. Whoever holds
// the audio guard is the plugin's audio thread, so the main thread can stop processing and reactivate the plugin
// while the audio thread skips its blocks, and neither ever waits on a lock the other holds for long.
class ClapInstrument {
public:
    struct Impl;

    static std::expected<std::shared_ptr<ClapInstrument>, std::string> create(const std::filesystem::path& bundle,
                                                                              const std::string& pluginId);

    explicit ClapInstrument(std::unique_ptr<Impl> impl);
    ~ClapInstrument();
    ClapInstrument(const ClapInstrument&) = delete;
    ClapInstrument& operator=(const ClapInstrument&) = delete;
    ClapInstrument(ClapInstrument&&) = delete;
    ClapInstrument& operator=(ClapInstrument&&) = delete;

    [[nodiscard]] const std::string& name() const;
    // Activates the plugin at this rate and largest block, reactivating it when either changed; false when it refuses.
    bool activate(double sampleRate, core::Frames maxFrames);
    // Runs what the plugin asked of the main thread: its callback, a restart and closing its editor. Returns true once
    // after the plugin marked its state changed.
    bool idle();
    // Nullopt when the plugin keeps no state or fails to save it.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> saveState() const;
    bool loadState(std::span<const std::uint8_t> bytes);

    // The params the plugin shows, leaving out hidden and read-only ones.
    [[nodiscard]] std::vector<ClapParam> params() const;
    [[nodiscard]] std::optional<double> paramValue(std::uint32_t id) const;
    [[nodiscard]] std::string paramText(std::uint32_t id, double value) const;
    // Main thread: queues the value for the next process call, or flushes it while the plugin is not processing, and
    // returns once the plugin has it, so a state saved next holds it.
    void setParam(std::uint32_t id, double value);
    // True once after the plugin asked to rescan its params or reported a param change of its own.
    bool takeParamsChanged();

    [[nodiscard]] bool hasEditor() const;
    [[nodiscard]] bool editorOpen() const;
    // Opens the plugin's own editor window when it has one; false when it only embeds.
    bool openFloatingEditor(const std::string& title);
    // Embeds the editor in parent, the platform's native view (NSView, X11 window or HWND), and returns its size.
    std::optional<EditorSize> embedEditor(void* parent);
    void closeEditor();
    // The size the embedded editor asked for since the last call.
    std::optional<EditorSize> takeEditorResize();

    // Realtime: queues a MIDI message for the next processBlock, which plays it at its timestamp in that block.
    void queueMidi(std::span<const std::uint8_t> bytes, std::int64_t timestampNs);
    // Realtime: adds the plugin's main output to each mix channel; a mix channel past its outputs takes the last one.
    void processBlock(std::span<float* const> mix, core::Frames frames, std::int64_t hostTimeNs);

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace emerick::engine
