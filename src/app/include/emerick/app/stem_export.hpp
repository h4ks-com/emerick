#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/engine/engine.hpp"
#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// The end of the last clip on any track.
core::Ticks projectEnd(const model::Document& document);

// Where exports of the session stop: the project end, or the end of the bar of the last MIDI event a track plays, such
// as a chord from a lane, when that event comes after the project end.
core::Ticks exportEnd(const engine::Session& session, const model::Document& document);

// Renders each audio track and each MIDI track with an instrument of the session alone and dry, with its clip gains,
// fades and track gain, and each bus with the tracks of the session that feed it, to <folder>/<track name>.wav, or
// "<folder>/01 <track name>.wav" numbered by track position. A track whose instrument takes MIDI from another track
// plays with that track, and a track that only sends MIDI on gets no stem. Each stem runs on its own offline engine
// with fresh instances of the instruments from the slots of context's document. Throws AppError when nothing can
// render.
std::vector<std::filesystem::path> exportStems(const engine::Session& session, const Registry& registry,
                                               const PlaybackContext& context, const std::filesystem::path& folder,
                                               double sampleRate, bool numbered = false);

struct LoudnessTarget {
    double lufs = -14;
    double ceilingDbtp = -1;
};

struct MixLevels {
    double integratedLufs = 0;
    double truePeakDbtp = 0;
};

// Renders every audio track, instrument track and bus of the session with sends, mute and solo applied to one stereo
// WAV. With a target, it turns the mix to the target loudness under the true peak ceiling and returns the levels the
// file ends at.
std::optional<MixLevels> exportMix(const engine::Session& session, const Registry& registry,
                                   const PlaybackContext& context, const std::filesystem::path& file, double sampleRate,
                                   const std::optional<LoudnessTarget>& target = {});

// One render's fresh instruments and offline engine. The UI thread constructs it, runs idleInstruments and destroys it,
// and any one thread runs process.
class OfflineRender {
public:
    struct InstrumentMessage {
        std::string trackId;
        std::vector<std::uint8_t> bytes;
    };

    OfflineRender(engine::Session session, const Registry& registry, const PlaybackContext& context, double sampleRate);
    // Renders a session whose tracks already hold their instruments.
    OfflineRender(engine::Session session, std::vector<std::shared_ptr<engine::ClapInstrument>> instruments,
                  double sampleRate);
    ~OfflineRender();
    OfflineRender(const OfflineRender&) = delete;
    OfflineRender& operator=(const OfflineRender&) = delete;
    OfflineRender(OfflineRender&&) = delete;
    OfflineRender& operator=(OfflineRender&&) = delete;

    // Renders frames from the start, calling idle between blocks, which must have the main thread run idleInstruments
    // before it returns. Returns no channels once stop is requested.
    std::vector<std::vector<float>> process(engine::Frames frames, const std::function<void()>& idle,
                                            const std::stop_token& stop = {});
    // Runs what the instruments asked of the UI thread, such as loading a voice after a program change.
    void idleInstruments();

private:
    void start(engine::Session session, double sampleRate);

    std::vector<std::shared_ptr<engine::ClapInstrument>> instruments_;
    std::vector<InstrumentMessage> chase_;
    engine::OfflineBackend* offline_ = nullptr;
    std::unique_ptr<engine::Engine> engine_;
};

// Sets up the render of the clip played through the instrument.
std::unique_ptr<OfflineRender> instrumentRender(std::shared_ptr<engine::ClapInstrument> instrument,
                                                engine::MidiClip clip, const core::TempoMap& tempo, double sampleRate);

// Sets up the render of one track or bus as its stem plays.
std::unique_ptr<OfflineRender> trackRender(const engine::Session& session, const Registry& registry,
                                           const PlaybackContext& context, const std::string& trackId,
                                           double sampleRate);

}  // namespace emerick::app
