#include <sndfile.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <iterator>
#include <numeric>
#include <ranges>

#include "emerick/engine/engine.hpp"
#include "emerick/engine/sound_file.hpp"
#include "rig.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;

namespace {

const emerick::core::TempoMap kTempo{120.0, {4, 4}};

std::vector<float> readWav(const std::filesystem::path& path) {
    SF_INFO info{};
    SNDFILE* file = openSoundFile(path, SFM_READ, &info);
    REQUIRE(file != nullptr);
    std::vector<float> samples(static_cast<std::size_t>(info.frames * info.channels));
    sf_readf_float(file, samples.data(), info.frames);
    sf_close(file);
    return samples;
}

Track audioTrack(double recordDelayMs = 0.0) {
    Track track{.id = "vox", .kind = TrackKind::audio, .armed = true, .recordDelayMs = recordDelayMs};
    track.audioIn = {"interface", {2}};
    return track;
}

Track midiTrack(double recordDelayMs = 0.0) {
    Track track{.id = "keys", .kind = TrackKind::midi, .armed = true, .recordDelayMs = recordDelayMs};
    track.midiIn = {kPort1, 1};
    return track;
}

std::vector<float> ramp(std::size_t frames) {
    std::vector<float> samples(frames);
    std::iota(samples.begin(), samples.end(), 1.0F);
    return samples;
}

RecordedTake recordOne(Rig& rig, Frames frames) {
    rig.engine.setRecording(true);
    rig.engine.play();
    rig.audio.process(frames);
    rig.engine.stop();
    rig.audio.process(kBlock);
    std::vector<RecordedTake> takes;
    REQUIRE(waitFor([&] {
        takes = rig.engine.takeRecordings();
        return !takes.empty();
    }));
    return takes.front();
}

Ticks recordedNoteTick(double recordDelayMs) {
    Rig rig;
    rig.engine.setSession({.tempo = kTempo, .tracks = {midiTrack(recordDelayMs)}});
    rig.engine.setRecording(true);
    rig.engine.play();
    rig.audio.process(kBlock);
    rig.midi.inject(kPort1, std::vector<std::uint8_t>{0x90, 60, 100}, rig.audio.hostTimeAt(74));
    rig.audio.process(kBlock * 10);
    rig.engine.stop();
    rig.audio.process(kBlock);
    std::vector<RecordedTake> takes;
    REQUIRE(waitFor([&] {
        takes = rig.engine.takeRecordings();
        return !takes.empty();
    }));
    REQUIRE(takes.front().midiEvents.size() == 1);
    return takes.front().midiEvents.front().tick;
}

}  // namespace

TEST_CASE("a take in progress reports its MIDI notes and audio peaks before the take ends") {
    Rig rig;
    const TempFolder folder;
    rig.engine.setSession({.tempo = kTempo, .tracks = {audioTrack(), midiTrack()}, .takeFolder = folder.path});
    rig.audio.setInput("interface", 2, std::vector<float>(48000, 0.5F));
    rig.engine.setRecording(true);
    rig.engine.play();
    rig.audio.process(kBlock);
    rig.midi.inject(kPort1, std::vector<std::uint8_t>{0x90, 60, 100}, rig.audio.hostTimeAt(74));
    rig.audio.process(kTakePeakFrames * 8);

    std::vector<MidiEvent> notes;
    std::vector<float> peaks;
    Frames frames = 0;
    std::filesystem::path file;
    REQUIRE(waitFor([&] {
        for (TakeProgress& progress : rig.engine.takeProgress()) {
            std::ranges::move(progress.midiEvents, std::back_inserter(notes));
            std::ranges::move(progress.peakMaximum, std::back_inserter(peaks));
            if (progress.trackId == "vox") {
                frames = progress.frames;
                file = progress.audioFile;
            }
        }
        return !notes.empty() && frames >= kTakePeakFrames * 8;
    }));
    CHECK(notes == std::vector<MidiEvent>{{3, {0x90, 60, 100}}});
    CHECK(file.parent_path() == folder.path);
    CHECK(std::ssize(peaks) == frames / kTakePeakFrames);
    CHECK(std::ranges::all_of(peaks, [](float peak) { return peak == 0.5F; }));

    rig.audio.process(kTakePeakFrames * 8);
    REQUIRE(waitFor([&] {
        for (const TakeProgress& progress : rig.engine.takeProgress()) {
            if (progress.trackId == "vox") {
                frames = progress.frames;
            }
        }
        return frames >= kTakePeakFrames * 16;
    }));
    rig.engine.stop();
    rig.audio.process(kBlock);
    REQUIRE(waitFor([&] { return rig.engine.takeRecordings().size() == 2; }));
    CHECK(rig.engine.takeProgress().empty());
}

TEST_CASE("a positive record delay starts an audio take with that many frames of silence") {
    Rig rig;
    const TempFolder folder;
    rig.engine.setSession({.tempo = kTempo, .tracks = {audioTrack(1.0)}, .takeFolder = folder.path});
    rig.audio.setInput("interface", 2, ramp(1000));

    const std::vector<float> recorded = readWav(recordOne(rig, 1000).audioFile);

    REQUIRE(recorded.size() > 48);
    CHECK(std::ranges::all_of(recorded | std::views::take(48), [](float sample) { return sample == 0.0F; }));
    CHECK(recorded[48] == 1.0F);
}

TEST_CASE("a negative record delay skips that many frames at the start of an audio take") {
    Rig rig;
    const TempFolder folder;
    rig.engine.setSession({.tempo = kTempo, .tracks = {audioTrack(-1.0)}, .takeFolder = folder.path});
    rig.audio.setInput("interface", 2, ramp(1000));

    const std::vector<float> recorded = readWav(recordOne(rig, 1000).audioFile);

    REQUIRE(!recorded.empty());
    CHECK(recorded[0] == 49.0F);
}

TEST_CASE("a record delay moves a MIDI take's events by its frames and never before the take starts") {
    CHECK(recordedNoteTick(0.0) == 3);
    CHECK(recordedNoteTick(20.0) == 41);
    CHECK(recordedNoteTick(-1.0) == 1);
    CHECK(recordedNoteTick(-500.0) == 0);
}
