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
#include "emerick/app/registry.hpp"
#include "emerick/engine/engine.hpp"
#include "emerick/model/clip_edit.hpp"

namespace emerick::app {

namespace {

constexpr engine::Frames kBlock = 512;
constexpr engine::Frames kChunk = 24000;
constexpr auto kReadAheadTimeout = std::chrono::seconds{5};
constexpr const char* kRenderDevice = "render";

std::string fileStem(const std::string& name) {
    std::string stem = name.empty() ? std::string{"track"} : name;
    std::ranges::replace_if(stem, [](char c) { return std::string_view{"/\\:*?\"<>|"}.contains(c); }, '_');
    return stem;
}

std::vector<engine::Track> audioTracks(const engine::Session& session) {
    return session.tracks |
           std::views::filter([](const engine::Track& track) { return track.kind == engine::TrackKind::audio; }) |
           std::ranges::to<std::vector>();
}

// We sleep between chunks so the clip reader can refill its 2 s read-ahead.
std::vector<std::vector<float>> render(engine::Session session, engine::Frames frames, double sampleRate) {
    for (engine::Track& track : session.tracks) {
        track.armed = false;
        track.monitor = false;
        track.audioIn = {};
        track.audioOut = {kRenderDevice, {1, 2}};
    }
    session.clockPorts.clear();
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
    offline.process(kBlock);
    const auto deadline = std::chrono::steady_clock::now() + kReadAheadTimeout;
    while (!engine.readAheadReady()) {
        if (std::chrono::steady_clock::now() > deadline) {
            throw AppError("cannot render: audio files did not load within 5 s");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    engine.play();
    const engine::Frames start = offline.processedFrames();
    for (engine::Frames done = 0; done < frames; done += kChunk) {
        offline.process(std::min(kChunk, frames - done));
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    std::vector<std::vector<float>> channels;
    for (const int channel : {1, 2}) {
        const std::vector<float> output = offline.output(kRenderDevice, channel);
        channels.emplace_back(output.begin() + start, output.begin() + start + frames);
    }
    return channels;
}

engine::Frames renderFrames(const model::Document& document, double sampleRate) {
    const core::Ticks end = projectEnd(document);
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

std::vector<std::filesystem::path> exportStems(const engine::Session& session, const model::Document& document,
                                               const std::filesystem::path& folder, double sampleRate, bool numbered) {
    const engine::Frames frames = renderFrames(document, sampleRate);
    std::vector<engine::Track> tracks = audioTracks(session);
    if (tracks.empty()) {
        throw AppError("there are no audio tracks to export");
    }
    std::filesystem::create_directories(folder);
    std::vector<std::filesystem::path> files;
    std::set<std::string> used;
    for (engine::Track& track : tracks) {
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
        track.mute = false;
        track.solo = false;
        engine::Session single{
            .tempo = session.tempo, .tracks = {track}, .clockPorts = {}, .takeFolder = {}, .metronome = {}};
        files.push_back(folder / (stem + ".wav"));
        writeWav(files.back(), render(std::move(single), frames, sampleRate), sampleRate);
    }
    return files;
}

void exportMix(const engine::Session& session, const model::Document& document, const std::filesystem::path& file,
               double sampleRate) {
    const engine::Frames frames = renderFrames(document, sampleRate);
    engine::Session mix{
        .tempo = session.tempo, .tracks = audioTracks(session), .clockPorts = {}, .takeFolder = {}, .metronome = {}};
    if (!file.parent_path().empty()) {
        std::filesystem::create_directories(file.parent_path());
    }
    writeWav(file, render(std::move(mix), frames, sampleRate), sampleRate);
}

}  // namespace emerick::app
