#include "emerick/app/stem_export.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <ranges>
#include <set>
#include <string>
#include <thread>
#include <utility>

#include "emerick/app/audio_file.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/engine/audio_analysis.hpp"
#include "emerick/engine/engine.hpp"
#include "emerick/model/clip_edit.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

namespace {

constexpr engine::Frames kBlock = 512;
constexpr engine::Frames kChunk = 24000;
constexpr int kSettleBlocks = 16;
constexpr auto kReadAheadTimeout = std::chrono::seconds{5};
constexpr const char* kRenderDevice = "render";

using RenderInstruments = std::vector<std::shared_ptr<engine::Plugin>>;

std::string fileStem(const std::string& name) {
    std::string stem = name.empty() ? std::string{"track"} : name;
    std::ranges::replace_if(stem, [](char c) { return std::string_view{"/\\:*?\"<>|"}.contains(c); }, '_');
    return stem;
}

bool hasInstrument(const model::Document& document, const std::string& id) {
    const model::Track* track = model::findTrack(document, model::TrackId{id});
    return track != nullptr && track->instrument.has_value();
}

bool playsAudio(const engine::Track& track, const model::Document& document) {
    return track.kind != engine::TrackKind::midi || hasInstrument(document, track.id);
}

// A track that sends MIDI on to another track's instrument, as an Arranger does, gets no stem of its own.
bool feedsMidi(const engine::Session& session, const engine::Track& track) {
    return std::ranges::contains(session.tracks, track.id, &engine::Track::midiInTrack);
}

std::vector<engine::Track> stemTracks(const engine::Session& session, const model::Document& document) {
    return session.tracks | std::views::filter([&](const engine::Track& track) {
               return playsAudio(track, document) && !feedsMidi(session, track);
           }) |
           std::ranges::to<std::vector>();
}

engine::AudioRoute renderRoute() { return {kRenderDevice, {1, 2}}; }

engine::AudioRoute mixRoute(const engine::Track& track, const model::Document& document) {
    return playsAudio(track, document) ? renderRoute() : engine::AudioRoute{};
}

engine::Track audible(engine::Track track) {
    track.mute = false;
    track.solo = false;
    return track;
}

engine::Session renderSession(const engine::Session& session, std::vector<engine::Track> tracks) {
    return {.tempo = session.tempo,
            .tracks = std::move(tracks),
            .clockPorts = {},
            .takeFolder = {},
            .metronome = {},
            .panLaw = session.panLaw,
            .taps = {}};
}

// A bus stem plays the bus alone with every track and bus feeding it, so only the bus reaches the render output.
std::vector<engine::Track> busStemTracks(const std::vector<engine::Track>& tracks, const engine::Track& bus) {
    return tracks | std::views::transform([&](const engine::Track& source) {
               engine::Track track = audible(source);
               track.audioOut = track.id == bus.id ? renderRoute() : engine::AudioRoute{};
               if (track.id == bus.id) {
                   track.outputBus.clear();
               }
               return track;
           }) |
           std::ranges::to<std::vector>();
}

engine::Track dry(const engine::Track& source, engine::AudioRoute out) {
    engine::Track track = audible(source);
    track.audioOut = std::move(out);
    track.outputBus.clear();
    track.sends.clear();
    return track;
}

// A track stem plays the track dry and straight to the render output, with the tracks that send it MIDI playing
// into no output.
std::vector<engine::Track> trackStemTracks(const std::vector<engine::Track>& tracks, const engine::Track& track) {
    std::vector<engine::Track> stem;
    for (std::string source = track.midiInTrack; !source.empty();) {
        const auto found = std::ranges::find(tracks, source, &engine::Track::id);
        if (found == tracks.end() || found->id == track.id || std::ranges::contains(stem, source, &engine::Track::id)) {
            break;
        }
        stem.push_back(dry(*found, {}));
        source = found->midiInTrack;
    }
    stem.push_back(dry(track, renderRoute()));
    return stem;
}

engine::Session stemSession(const engine::Session& session, const engine::Track& track) {
    return renderSession(session, track.kind == engine::TrackKind::bus ? busStemTracks(session.tracks, track)
                                                                       : trackStemTracks(session.tracks, track));
}

engine::Session mixSession(const engine::Session& session, const model::Document& document) {
    std::vector<engine::Track> tracks = session.tracks;
    for (engine::Track& track : tracks) {
        track.audioOut = mixRoute(track, document);
    }
    return renderSession(session, std::move(tracks));
}

// Every track plays unmuted into its buses and sends as in a bus stem, so each listed track's own output sounds as its
// stem. With mixOf, output channels 1 and 2 also carry the mix of every track that plays audio in that document, and
// the listed tracks move up two channels.
engine::Session captureSession(const engine::Session& session, const std::vector<std::string>& trackIds,
                               const model::Document* mixOf = nullptr) {
    std::vector<engine::Track> tracks =
        session.tracks | std::views::transform(audible) | std::ranges::to<std::vector>();
    const int offset = mixOf != nullptr ? 2 : 0;
    for (engine::Track& track : tracks) {
        track.audioOut = mixOf != nullptr ? mixRoute(track, *mixOf) : engine::AudioRoute{};
        if (const auto found = std::ranges::find(trackIds, track.id); found != trackIds.end()) {
            const auto first = static_cast<int>(2 * (found - trackIds.begin())) + 1 + offset;
            track.captureOut = {kRenderDevice, {first, first + 1}};
        }
    }
    return renderSession(session, std::move(tracks));
}

std::shared_ptr<engine::Plugin> renderPlugin(const model::Track& track, const model::PluginSlot& slot) {
    auto plugin = engine::createPlugin(slot.format, slot.bundle, slot.pluginId);
    if (!plugin) {
        throw AppError("cannot render track " + track.name + ": " + plugin.error());
    }
    if (!slot.state.empty() && !(*plugin)->loadState(slot.state)) {
        throw AppError("cannot render track " + track.name + ": " + slot.name + " did not load its settings");
    }
    return *std::move(plugin);
}

// Each render plays fresh instances with the saved states, so the live plugins stay with the live engine.
RenderInstruments attachInstruments(engine::Session& session, const model::Document& document) {
    RenderInstruments created;
    for (engine::Track& track : session.tracks) {
        const model::Track* source = model::findTrack(document, model::TrackId{track.id});
        if (source == nullptr) {
            continue;
        }
        if (source->instrument) {
            track.instrument = renderPlugin(*source, *source->instrument);
            created.push_back(track.instrument);
        }
        std::map<std::string, std::shared_ptr<engine::Plugin>> inserts;
        for (const model::Insert& insert : source->inserts) {
            if (insert.plugin) {
                inserts[insert.id.value] = renderPlugin(*source, *insert.plugin);
                created.push_back(inserts[insert.id.value]);
            }
        }
        attachPluginInserts(track, *source, [&inserts](const model::InsertId& id) {
            const auto found = inserts.find(id.value);
            return found == inserts.end() ? nullptr : found->second;
        });
    }
    for (engine::Track& track : session.tracks) {
        if (!hasInstrument(document, track.midiInTrack)) {
            track.midiInTrack.clear();
        }
    }
    return created;
}

// Track types put their devices in their state at bar 1, such as a Style's first section, as they do on play.
std::vector<OfflineRender::InstrumentMessage> chaseStart(const engine::Session& session, const Registry& registry,
                                                         const PlaybackContext& context) {
    std::vector<OfflineRender::InstrumentMessage> messages;
    for (const engine::Track& track : session.tracks) {
        const model::Track* source = model::findTrack(*context.document, model::TrackId{track.id});
        const TrackType* type = source != nullptr ? registry.trackType(source->kind) : nullptr;
        if (!track.instrument || type == nullptr || !type->chase) {
            continue;
        }
        for (MidiMessage& message : type->chase(*source, 0, context)) {
            if (message.port.empty()) {
                messages.push_back({.trackId = track.id, .bytes = std::move(message.bytes)});
            }
        }
    }
    return messages;
}

void waitForReadAhead(const engine::Engine& engine) {
    const auto deadline = std::chrono::steady_clock::now() + kReadAheadTimeout;
    while (!engine.readAheadReady()) {
        if (std::chrono::steady_clock::now() > deadline) {
            throw AppError("cannot render: audio files did not load within 5 s");
        }
        std::this_thread::yield();
    }
}

engine::Frames renderFrames(const engine::Session& session, const model::Document& document, const Registry& registry,
                            double sampleRate) {
    const core::Ticks end = exportEnd(session, document);
    if (end <= 0) {
        throw AppError("the project has no clips to export");
    }
    registry.checkRenderSeconds(document.tempo.ticksToSeconds(end));
    return document.tempo.ticksToFrames(end, sampleRate);
}

}  // namespace

model::Document liveDocument(ProjectController& controller) {
    return controller.instruments().withStates(controller.project().document());
}

PlaybackContext playbackContext(ProjectController& controller, const model::Document& document) {
    return {.tempo = document.tempo, .folder = controller.folder(), .document = &document};
}

core::Ticks exportEnd(const engine::Session& session, const model::Document& document) {
    core::Ticks last = -1;
    for (const engine::Track& track : session.tracks) {
        for (const engine::MidiClip& clip : track.midiClips) {
            if (!clip.events.empty()) {
                last = std::max(last, clip.start + clip.events.back().tick);
            }
            if (clip.length > 0) {
                last = std::max(last, clip.start + clip.length - 1);
            }
        }
    }
    const core::Ticks clipsEnd = projectEnd(document);
    const core::Ticks bar = document.tempo.ticksPerBar();
    return last < 0 || (clipsEnd > 0 && last <= clipsEnd) ? clipsEnd : ((last / bar) + 1) * bar;
}

std::vector<std::filesystem::path> exportStems(const engine::Session& session, const Registry& registry,
                                               const PlaybackContext& context, const std::filesystem::path& folder,
                                               double sampleRate, bool numbered, AudioEncoding encoding) {
    const model::Document& document = *context.document;
    const engine::Frames frames = renderFrames(session, document, registry, sampleRate);
    const std::vector<engine::Track> tracks = stemTracks(session, document);
    if (tracks.empty()) {
        throw AppError("there are no audio tracks to export");
    }
    std::filesystem::create_directories(folder);
    std::vector<std::filesystem::path> files;
    std::set<std::string> used;
    for (const engine::Track& track : tracks) {
        const auto source = std::ranges::find(document.tracks, model::TrackId{track.id}, &model::Track::id);
        std::string stem = fileStem(source != document.tracks.end() ? source->name : track.id);
        if (numbered) {
            const std::string number = std::format("{:02} ", source - document.tracks.begin() + 1);
            if (!stem.starts_with(number)) {
                stem.insert(0, number);
            }
        }
        if (!used.insert(stem).second) {
            stem += "-" + track.id;
        }
        files.push_back(folder / std::format("{}.{}", stem, audioFormatName(encoding.format)));
    }
    std::vector<AudioWriter> writers = files | std::views::transform([&](const std::filesystem::path& file) {
                                           return AudioWriter(file, 2, sampleRate, encoding);
                                       }) |
                                       std::ranges::to<std::vector>();
    OfflineRender render(
        captureSession(session, tracks | std::views::transform(&engine::Track::id) | std::ranges::to<std::vector>()),
        registry, context, sampleRate);
    render.process(
        frames,
        [&writers](const std::vector<std::vector<float>>& chunk) {
            for (std::size_t index = 0; index < writers.size(); ++index) {
                writers[index].write(std::span{chunk}.subspan(2 * index, 2));
            }
        },
        [&render] { render.idleInstruments(); });
    return files;
}

std::optional<MixLevels> exportMix(const engine::Session& session, const Registry& registry,
                                   const PlaybackContext& context, const std::filesystem::path& file, double sampleRate,
                                   const std::optional<LoudnessTarget>& target, AudioEncoding encoding) {
    const engine::Frames frames = renderFrames(session, *context.document, registry, sampleRate);
    if (!file.parent_path().empty()) {
        std::filesystem::create_directories(file.parent_path());
    }
    AudioWriter writer(file, 2, sampleRate, encoding);
    std::vector<std::vector<float>> mix(2);
    OfflineRender render(mixSession(session, *context.document), registry, context, sampleRate);
    render.process(
        frames, [&mix](const std::vector<std::vector<float>>& chunk) { appendStereo(mix, chunk); },
        [&render] { render.idleInstruments(); });
    std::optional<MixLevels> levels;
    if (target) {
        const double lufs = engine::matchLoudness(mix, sampleRate, target->lufs, target->ceilingDbtp);
        levels = MixLevels{.integratedLufs = lufs, .truePeakDbtp = engine::truePeakDbtp(mix)};
    }
    writer.write(mix);
    return levels;
}

OfflineRender::OfflineRender(engine::Session session, const Registry& registry, const PlaybackContext& context,
                             double sampleRate) {
    instruments_ = attachInstruments(session, *context.document);
    chase_ = chaseStart(session, registry, context);
    start(std::move(session), sampleRate);
}

OfflineRender::OfflineRender(engine::Session session, std::vector<std::shared_ptr<engine::Plugin>> instruments,
                             double sampleRate)
    : instruments_(std::move(instruments)) {
    start(std::move(session), sampleRate);
}

void OfflineRender::start(engine::Session session, double sampleRate) {
    for (engine::Track& track : session.tracks) {
        track.armed = false;
        track.monitor = false;
        track.audioIn = {};
        track.midiIn.portId.clear();
        track.midiOut = {};
        track.midiThru = false;
    }
    session.clockPorts.clear();
    int outputs = 2;
    for (const engine::Track& track : session.tracks) {
        outputs =
            std::ranges::fold_left(track.captureOut.channels, outputs, [](int a, int b) { return std::max(a, b); });
    }
    auto backend = std::make_unique<engine::OfflineBackend>(
        std::vector<engine::AudioDeviceInfo>{{kRenderDevice, "Render", 0, outputs}});
    offline_ = backend.get();
    engine_ = std::make_unique<engine::Engine>(
        std::move(backend),
        std::make_unique<engine::VirtualMidiBackend>(std::vector<std::string>{}, std::vector<std::string>{}),
        engine::Engine::Options{.sampleRate = sampleRate, .blockFrames = kBlock});
    try {
        engine_->setSession(std::move(session));
    } catch (const std::invalid_argument& error) {
        throw AppError(std::string{"cannot render: "} + error.what());
    } catch (const std::runtime_error& error) {
        throw AppError(std::string{"cannot render: "} + error.what());
    }
    if (!engine_->sessionErrors().empty()) {
        std::string message = "cannot render every clip:";
        for (const engine::SessionError& skipped : engine_->sessionErrors()) {
            message += " track " + skipped.trackId + ": " + skipped.message + ";";
        }
        throw AppError(message);
    }
    // We open the stream here, so the instruments activate on this thread and process never has to.
    engine_->keepWarm(true);
    engine_->locate(0);
}

OfflineRender::~OfflineRender() = default;

void OfflineRender::process(engine::Frames frames, const ChunkSink& sink, const std::function<void()>& idle,
                            const std::stop_token& stop) {
    // CLAP offers no way to ask a plugin whether it is ready, so we let the instruments settle for a few blocks.
    for (int block = 0; block < kSettleBlocks; ++block) {
        offline_->process(kBlock);
        idle();
    }
    waitForReadAhead(*engine_);
    for (const InstrumentMessage& message : chase_) {
        engine_->sendInstrumentMidi(message.trackId, message.bytes);
    }
    engine_->play();
    offline_->takeOutputs();
    for (engine::Frames done = 0; done < frames; done += kChunk) {
        const engine::Frames chunk = std::min(kChunk, frames - done);
        for (engine::Frames block = 0; block < chunk; block += kBlock) {
            if (stop.stop_requested()) {
                return;
            }
            offline_->process(std::min(kBlock, chunk - block));
            idle();
        }
        waitForReadAhead(*engine_);
        sink(offline_->takeOutputs());
    }
}

void appendStereo(std::vector<std::vector<float>>& stereo, const std::vector<std::vector<float>>& chunk) {
    for (const std::size_t channel : {0U, 1U}) {
        stereo[channel].insert(stereo[channel].end(), chunk[channel].begin(), chunk[channel].end());
    }
}

void OfflineRender::idleInstruments() {
    for (const auto& instrument : instruments_) {
        instrument->idle();
    }
}

std::unique_ptr<OfflineRender> instrumentRender(std::shared_ptr<engine::Plugin> instrument, engine::MidiClip clip,
                                                const core::TempoMap& tempo, double sampleRate) {
    engine::Track track;
    track.id = "render";
    track.kind = engine::TrackKind::midi;
    track.audioOut = renderRoute();
    track.midiClips.push_back(std::move(clip));
    track.instrument = instrument;
    engine::Session session;
    session.tempo = tempo;
    session.tracks.push_back(std::move(track));
    return std::make_unique<OfflineRender>(
        std::move(session), std::vector<std::shared_ptr<engine::Plugin>>{std::move(instrument)}, sampleRate);
}

std::unique_ptr<OfflineRender> trackRender(const engine::Session& session, const Registry& registry,
                                           const PlaybackContext& context, const std::string& trackId,
                                           double sampleRate) {
    const auto track = std::ranges::find(session.tracks, trackId, &engine::Track::id);
    if (track == session.tracks.end()) {
        throw AppError("no track " + trackId + " in the session");
    }
    return std::make_unique<OfflineRender>(stemSession(session, *track), registry, context, sampleRate);
}

std::unique_ptr<OfflineRender> tracksRender(const engine::Session& session, const Registry& registry,
                                            const PlaybackContext& context, const std::vector<std::string>& trackIds,
                                            double sampleRate) {
    return std::make_unique<OfflineRender>(captureSession(session, trackIds), registry, context, sampleRate);
}

std::unique_ptr<OfflineRender> mixAndTracksRender(const engine::Session& session, const Registry& registry,
                                                  const PlaybackContext& context,
                                                  const std::vector<std::string>& trackIds, double sampleRate) {
    return std::make_unique<OfflineRender>(captureSession(session, trackIds, context.document), registry, context,
                                           sampleRate);
}

}  // namespace emerick::app
