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

using RenderInstruments = std::vector<std::shared_ptr<engine::ClapInstrument>>;

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

// A bus stem plays the bus alone with every track and bus feeding it, so only the bus reaches the render output.
std::vector<engine::Track> busStemTracks(const std::vector<engine::Track>& tracks, const engine::Track& bus) {
    return tracks | std::views::transform([&](engine::Track track) {
               track.mute = false;
               track.solo = false;
               track.audioOut = track.id == bus.id ? renderRoute() : engine::AudioRoute{};
               if (track.id == bus.id) {
                   track.outputBus.clear();
               }
               return track;
           }) |
           std::ranges::to<std::vector>();
}

engine::Track dry(engine::Track track, engine::AudioRoute out) {
    track.mute = false;
    track.solo = false;
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
    return {.tempo = session.tempo,
            .tracks = track.kind == engine::TrackKind::bus ? busStemTracks(session.tracks, track)
                                                           : trackStemTracks(session.tracks, track),
            .clockPorts = {},
            .takeFolder = {},
            .metronome = {},
            .panLaw = session.panLaw};
}

engine::Session mixSession(const engine::Session& session, const model::Document& document) {
    std::vector<engine::Track> tracks = session.tracks;
    for (engine::Track& track : tracks) {
        track.audioOut = playsAudio(track, document) ? renderRoute() : engine::AudioRoute{};
    }
    return {.tempo = session.tempo,
            .tracks = std::move(tracks),
            .clockPorts = {},
            .takeFolder = {},
            .metronome = {},
            .panLaw = session.panLaw};
}

// Each render plays fresh instances with the saved states, so the live instruments stay with the live engine.
RenderInstruments attachInstruments(engine::Session& session, const model::Document& document) {
    RenderInstruments created;
    for (engine::Track& track : session.tracks) {
        const model::Track* source = model::findTrack(document, model::TrackId{track.id});
        if (source == nullptr || !source->instrument) {
            continue;
        }
        const model::PluginSlot& slot = *source->instrument;
        auto instrument = engine::ClapInstrument::create(slot.bundle, slot.pluginId);
        if (!instrument) {
            throw AppError("cannot render track " + source->name + ": " + instrument.error());
        }
        if (!slot.state.empty() && !(*instrument)->loadState(slot.state)) {
            throw AppError("cannot render track " + source->name + ": " + slot.name + " did not load its settings");
        }
        track.instrument = *instrument;
        created.push_back(std::move(*instrument));
    }
    for (engine::Track& track : session.tracks) {
        if (!hasInstrument(document, track.midiInTrack)) {
            track.midiInTrack.clear();
        }
    }
    return created;
}

// Plugins do main-thread work, such as loading a voice after a program change, through host callbacks.
void idle(const RenderInstruments& instruments) {
    for (const auto& instrument : instruments) {
        instrument->idle();
    }
}

// Track types put their devices in their state at bar 1, such as a Style's first section, as they do on play.
void chaseStart(engine::Engine& engine, const engine::Session& session, const Registry& registry,
                const PlaybackContext& context) {
    for (const engine::Track& track : session.tracks) {
        const model::Track* source = model::findTrack(*context.document, model::TrackId{track.id});
        const TrackType* type = source != nullptr ? registry.trackType(source->kind) : nullptr;
        if (!track.instrument || type == nullptr || !type->chase) {
            continue;
        }
        for (const MidiMessage& message : type->chase(*source, 0, context)) {
            if (message.port.empty()) {
                engine.sendInstrumentMidi(track.id, message.bytes);
            }
        }
    }
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

std::vector<std::vector<float>> render(engine::Session session, const Registry& registry,
                                       const PlaybackContext& context, engine::Frames frames, double sampleRate) {
    for (engine::Track& track : session.tracks) {
        track.armed = false;
        track.monitor = false;
        track.audioIn = {};
        track.midiIn.portId.clear();
        track.midiOut = {};
        track.midiThru = false;
    }
    session.clockPorts.clear();
    const RenderInstruments instruments = attachInstruments(session, *context.document);
    const engine::Session played = session;
    auto backend =
        std::make_unique<engine::OfflineBackend>(std::vector<engine::AudioDeviceInfo>{{kRenderDevice, "Render", 0, 2}});
    engine::OfflineBackend& offline = *backend;
    engine::Engine engine(
        std::move(backend),
        std::make_unique<engine::VirtualMidiBackend>(std::vector<std::string>{}, std::vector<std::string>{}),
        {sampleRate, kBlock});
    try {
        engine.setSession(std::move(session));
    } catch (const std::invalid_argument& error) {
        throw AppError(std::string{"cannot render: "} + error.what());
    } catch (const std::runtime_error& error) {
        throw AppError(std::string{"cannot render: "} + error.what());
    }
    if (!engine.sessionErrors().empty()) {
        std::string message = "cannot render every clip:";
        for (const engine::SessionError& skipped : engine.sessionErrors()) {
            message += " track " + skipped.trackId + ": " + skipped.message + ";";
        }
        throw AppError(message);
    }
    engine.locate(0);
    // CLAP offers no way to ask a plugin whether it is ready, so we let the instruments settle for a few blocks.
    for (int block = 0; block < kSettleBlocks; ++block) {
        offline.process(kBlock);
        idle(instruments);
    }
    waitForReadAhead(engine);
    chaseStart(engine, played, registry, context);
    engine.play();
    const engine::Frames start = offline.processedFrames();
    for (engine::Frames done = 0; done < frames; done += kChunk) {
        const engine::Frames chunk = std::min(kChunk, frames - done);
        for (engine::Frames block = 0; block < chunk; block += kBlock) {
            offline.process(std::min(kBlock, chunk - block));
            idle(instruments);
        }
        waitForReadAhead(engine);
    }
    std::vector<std::vector<float>> channels;
    for (const int channel : {1, 2}) {
        const std::vector<float> output = offline.output(kRenderDevice, channel);
        channels.emplace_back(output.begin() + start, output.begin() + start + frames);
    }
    return channels;
}

engine::Frames renderFrames(const engine::Session& session, const model::Document& document, double sampleRate) {
    const core::Ticks end = exportEnd(session, document);
    if (end <= 0) {
        throw AppError("the project has no clips to export");
    }
    return document.tempo.ticksToFrames(end, sampleRate);
}

}  // namespace

core::Ticks projectEnd(const model::Document& document) {
    core::Ticks end = 0;
    for (const model::Track& track : document.tracks) {
        for (const model::Clip& clip : track.clips) {
            end = std::max(end, model::clipEnd(clip));
        }
    }
    return end;
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
                                               double sampleRate, bool numbered) {
    const model::Document& document = *context.document;
    const engine::Frames frames = renderFrames(session, document, sampleRate);
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
        files.push_back(folder / (stem + ".wav"));
        writeWav(files.back(), render(stemSession(session, track), registry, context, frames, sampleRate), sampleRate);
    }
    return files;
}

void exportMix(const engine::Session& session, const Registry& registry, const PlaybackContext& context,
               const std::filesystem::path& file, double sampleRate) {
    const engine::Frames frames = renderFrames(session, *context.document, sampleRate);
    if (!file.parent_path().empty()) {
        std::filesystem::create_directories(file.parent_path());
    }
    writeWav(file, render(mixSession(session, *context.document), registry, context, frames, sampleRate), sampleRate);
}

std::vector<std::vector<float>> renderTrack(const engine::Session& session, const Registry& registry,
                                            const PlaybackContext& context, const std::string& trackId,
                                            engine::Frames frames, double sampleRate) {
    if (trackId.empty()) {
        return render(mixSession(session, *context.document), registry, context, frames, sampleRate);
    }
    const auto track = std::ranges::find(session.tracks, trackId, &engine::Track::id);
    if (track == session.tracks.end()) {
        throw AppError("no track " + trackId + " in the session");
    }
    return render(stemSession(session, *track), registry, context, frames, sampleRate);
}

}  // namespace emerick::app
