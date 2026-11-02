#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/audio_file.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/engine/engine.hpp"
#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

class ProjectController;

// The project with each instrument's state as it plays now, which a render plays with fresh instances.
model::Document liveDocument(ProjectController& controller);
PlaybackContext playbackContext(ProjectController& controller, const model::Document& document);

// The end of the last clip on any track.
core::Ticks projectEnd(const model::Document& document);

// Where exports of the session stop: the project end, or the end of the bar of the last MIDI event a track plays, such
// as a chord from a lane, when that event comes after the project end.
core::Ticks exportEnd(const engine::Session& session, const model::Document& document);

// Renders each audio track and each MIDI track with an instrument of the session alone and dry, with its clip gains,
// fades and track gain, and each bus with the tracks of the session that feed it, to <folder>/<track name>.wav, or
// "<folder>/01 <track name>.wav" numbered by track position, with the suffix of the encoding's format. A track whose
// instrument takes MIDI from another track plays with that track, and a track that only sends MIDI on gets no stem. One
// offline engine renders every stem at once with fresh instances of the instruments from the slots of context's
// document. Throws AppError when nothing can render.
std::vector<std::filesystem::path> exportStems(const engine::Session& session, const Registry& registry,
                                               const PlaybackContext& context, const std::filesystem::path& folder,
                                               double sampleRate, bool numbered = false, AudioEncoding encoding = {});

struct LoudnessTarget {
    double lufs = -14;
    double ceilingDbtp = -1;
};

// The loudness target params ask for with normalize, targetLufs or ceilingDbtp, each defaulting to its export setting,
// or none.
std::optional<LoudnessTarget> loudnessTarget(const Registry& registry, const Json& params);

struct MixLevels {
    double integratedLufs = 0;
    double truePeakDbtp = 0;
};

// Renders every audio track, instrument track and bus of the session with sends, mute and solo applied to one stereo
// file in the encoding. With a target, it turns the mix to the target loudness under the true peak ceiling and returns
// the levels the file ends at.
std::optional<MixLevels> exportMix(const engine::Session& session, const Registry& registry,
                                   const PlaybackContext& context, const std::filesystem::path& file, double sampleRate,
                                   const std::optional<LoudnessTarget>& target = {}, AudioEncoding encoding = {});

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
    OfflineRender(engine::Session session, std::vector<std::shared_ptr<engine::Plugin>> instruments, double sampleRate);
    ~OfflineRender();
    OfflineRender(const OfflineRender&) = delete;
    OfflineRender& operator=(const OfflineRender&) = delete;
    OfflineRender(OfflineRender&&) = delete;
    OfflineRender& operator=(OfflineRender&&) = delete;

    // Takes every output channel of one chunk of a render.
    using ChunkSink = std::function<void(std::vector<std::vector<float>>)>;

    // Renders frames from the start and hands sink each chunk of every output channel in order, calling idle between
    // blocks, which must have the main thread run idleInstruments before it returns. Stops once stop is requested.
    void process(engine::Frames frames, const ChunkSink& sink, const std::function<void()>& idle,
                 const std::stop_token& stop = {});
    // Runs what the instruments asked of the UI thread, such as loading a voice after a program change.
    void idleInstruments();

private:
    void start(engine::Session session, double sampleRate);

    std::vector<std::shared_ptr<engine::Plugin>> instruments_;
    std::vector<InstrumentMessage> chase_;
    engine::OfflineBackend* offline_ = nullptr;
    std::unique_ptr<engine::Engine> engine_;
};

// Appends the first two channels of a render chunk to stereo, which holds two channels.
void appendStereo(std::vector<std::vector<float>>& stereo, const std::vector<std::vector<float>>& chunk);

// Sets up the render of the clip played through the instrument.
std::unique_ptr<OfflineRender> instrumentRender(std::shared_ptr<engine::Plugin> instrument, engine::MidiClip clip,
                                                const core::TempoMap& tempo, double sampleRate);

// Sets up the render of one track or bus as its stem plays.
std::unique_ptr<OfflineRender> trackRender(const engine::Session& session, const Registry& registry,
                                           const PlaybackContext& context, const std::string& trackId,
                                           double sampleRate);

// Sets up one render of the whole session in which output channels 2i + 1 and 2i + 2 carry the i-th listed track or
// bus as its stem plays it.
std::unique_ptr<OfflineRender> tracksRender(const engine::Session& session, const Registry& registry,
                                            const PlaybackContext& context, const std::vector<std::string>& trackIds,
                                            double sampleRate);

// Sets up one render of the whole session with every track unmuted, in which output channels 1 and 2 carry the mix and
// channels 2i + 3 and 2i + 4 the i-th listed track or bus as its stem plays it.
std::unique_ptr<OfflineRender> mixAndTracksRender(const engine::Session& session, const Registry& registry,
                                                  const PlaybackContext& context,
                                                  const std::vector<std::string>& trackIds, double sampleRate);

}  // namespace emerick::app
