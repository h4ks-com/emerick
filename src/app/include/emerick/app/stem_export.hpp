#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/audio_file.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/engine/audio_analysis.hpp"
#include "emerick/engine/engine.hpp"
#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

class ProjectController;

// The project with each instrument's state as it plays now, which a render plays with fresh instances.
model::Document liveDocument(ProjectController& controller);
PlaybackContext playbackContext(ProjectController& controller, const model::Document& document);

// Where exports of the session stop: the project end, or the end of the bar of the last MIDI event a track plays, such
// as a chord from a lane, when that event comes after the project end.
core::Ticks exportEnd(const engine::Session& session, const model::Document& document);

// The frames a render of the session plays up to its export end. Throws AppError when the project has no clips or the
// render is longer than the render limit.
engine::Frames renderFrames(const engine::Session& session, const model::Document& document, const Registry& registry,
                            double sampleRate);

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

// Keeps a render in a temporary file, so loudness matching can play it several times while memory stays flat.
class SpilledAudio {
public:
    explicit SpilledAudio(std::size_t channels);

    void append(std::span<const std::vector<float>> chunk);
    // Hands the sink the chunks in the sizes they were appended in.
    void replay(const engine::ChunkSink& sink) const;

private:
    struct CloseFile {
        void operator()(std::FILE* file) const;
    };

    std::size_t channels_;
    std::unique_ptr<std::FILE, CloseFile> file_;
    std::vector<std::size_t> chunkFrames_;
};

// Writes a stereo mix to writer, turned to the target as exportMix does, and returns the levels it ends at.
std::optional<MixLevels> writeMix(const SpilledAudio& mix, AudioWriter& writer, double sampleRate,
                                  const std::optional<LoudnessTarget>& target);

// One render's fresh instruments and offline engine. The UI thread constructs it, runs idleInstruments and destroys it,
// and any one thread runs process.
class OfflineRender {
public:
    struct InstrumentMessage {
        std::string trackId;
        std::vector<std::uint8_t> bytes;
    };

    // With pipelineBuses, a second thread mixes the buses a block behind the tracks. The render's device takes
    // inputChannels inputs, which a track listens to live when its audio input names that device.
    OfflineRender(engine::Session session, const Registry& registry, const PlaybackContext& context, double sampleRate,
                  bool pipelineBuses = true, int inputChannels = 0);
    // Renders a session whose tracks already hold their instruments.
    OfflineRender(engine::Session session, std::vector<std::shared_ptr<engine::Plugin>> instruments, double sampleRate);
    ~OfflineRender();
    OfflineRender(const OfflineRender&) = delete;
    OfflineRender& operator=(const OfflineRender&) = delete;
    OfflineRender(OfflineRender&&) = delete;
    OfflineRender& operator=(OfflineRender&&) = delete;

    // Takes every output channel of one chunk of a render.
    using ChunkSink = std::function<void(std::vector<std::vector<float>>)>;
    // Returns every input channel of the next chunk of frames frames.
    using ChunkFeed = std::function<std::vector<std::vector<float>>(engine::Frames frames)>;
    // The device a render plays to and takes its inputs from.
    static constexpr const char* kDevice = "render";

    // Renders frames from the start and hands sink each chunk of every output channel in order, calling idle between
    // blocks, which must have the main thread run idleInstruments before it returns. Stops once stop is requested. With
    // feed, the blocks the instruments settle in before the start, and then each chunk, play the inputs feed returns
    // for them, and settled takes what the settling blocks output.
    void process(engine::Frames frames, const ChunkSink& sink, const std::function<void()>& idle,
                 const std::stop_token& stop = {}, const ChunkFeed& feed = {}, const ChunkSink& settled = {});
    // Runs what the instruments asked of the UI thread, such as loading a voice after a program change.
    void idleInstruments();
    // Any thread: true while an instrument may have asked for work idleInstruments runs.
    [[nodiscard]] bool needsIdle() const;
    // The frames the render delays its tracks by to line them up with its slowest one.
    [[nodiscard]] engine::Frames latencyCompensation() const { return engine_->latencyCompensation(); }
    // Plays frames from the start on the calling thread, which must be the UI thread, and returns the take of each
    // listed track that recorded MIDI from the track it takes MIDI from.
    std::vector<engine::RecordedTake> recordMidi(const std::vector<std::string>& trackIds, engine::Frames frames);

private:
    void start(engine::Session session, double sampleRate, bool pipelineBuses, int inputChannels);
    // Plays frames from the start and hands sink each chunk as the engine outputs it, late by its output delay.
    void play(engine::Frames frames, const ChunkSink& sink, const std::function<void()>& idle,
              const std::stop_token& stop, const ChunkFeed& feed, const ChunkSink& settled);

    std::vector<std::shared_ptr<engine::Plugin>> instruments_;
    std::vector<InstrumentMessage> chase_;
    engine::OfflineBackend* offline_ = nullptr;
    std::unique_ptr<engine::Engine> engine_;
};

// Plays the session offline from the start to its export end, with no audio output, and returns the take of each listed
// track that recorded MIDI from the track it takes MIDI from.
std::vector<engine::RecordedTake> recordMidiOffline(const engine::Session& session, const Registry& registry,
                                                    const PlaybackContext& context,
                                                    const std::vector<std::string>& trackIds, double sampleRate);

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

// The cores this process may use: a container's CPU quota can allow fewer than the machine has.
std::size_t availableCores();

// The signal before its fader of each track nothing feeds, from the last render of the mix and tracks, in a temporary
// folder, so a later render whose sources play the same plays them from there.
class SourceStems {
public:
    SourceStems();
    ~SourceStems();
    SourceStems(const SourceStems&) = delete;
    SourceStems& operator=(const SourceStems&) = delete;
    SourceStems(SourceStems&&) = delete;
    SourceStems& operator=(SourceStems&&) = delete;

    [[nodiscard]] std::filesystem::path file(const std::string& trackId) const;
    // True while the files hold whole stems of the sources key names.
    [[nodiscard]] bool holds(const std::string& key) const { return !key.empty() && key == key_; }
    // The files are being written, or failed to read back, so they hold nothing whole until keep.
    void forget() { key_.clear(); }
    void keep(std::string key) { key_ = std::move(key); }

private:
    std::filesystem::path folder_;
    std::string key_;
};

// UI thread: renders what one render of the session's tracks, all unmuted, would hand sink: the mix of every track
// that plays audio on channels 1 and 2 and each listed track's own output after it, for frames from the start. Each
// track nothing feeds plays alone on its own core, with the tracks it takes MIDI from, into a stem of its signal
// before its fader; then each bus plays alone from the stems that feed it once they exist, and the last render plays
// every stem through its fader. A loop of buses plays in one render. With stems, the sources play from them when
// sourcesKey, which names everything the sources play, matches what they hold, and render into them otherwise.
void renderMixAndTracks(const engine::Session& session, const Registry& registry, const PlaybackContext& context,
                        const std::vector<std::string>& trackIds, double sampleRate, engine::Frames frames,
                        const OfflineRender::ChunkSink& sink, SourceStems* stems = nullptr,
                        const std::string& sourcesKey = {});

}  // namespace emerick::app
