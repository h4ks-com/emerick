#include <sndfile.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

#include "emerick/engine/engine.hpp"
#include "rig.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;
using Catch::Approx;

namespace {

const emerick::core::TempoMap kTempo{120.0, {4, 4}};

std::vector<float> readWav(const std::filesystem::path& path) {
    SF_INFO info{};
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
    REQUIRE(file != nullptr);
    std::vector<float> samples(static_cast<std::size_t>(info.frames * info.channels));
    sf_readf_float(file, samples.data(), info.frames);
    sf_close(file);
    return samples;
}

void writeWav(const std::filesystem::path& path, const std::vector<float>& samples, double rate = kRate) {
    SF_INFO info{.samplerate = static_cast<int>(rate), .channels = 1, .format = SF_FORMAT_WAV | SF_FORMAT_FLOAT};
    SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &info);
    REQUIRE(file != nullptr);
    sf_writef_float(file, samples.data(), static_cast<sf_count_t>(samples.size()));
    sf_close(file);
}

void playFromStart(Rig& rig) {
    rig.engine.locate(0);
    rig.audio.process(kBlock);
    REQUIRE(waitFor([&] { return rig.engine.readAheadReady(); }));
    rig.engine.play();
}

Track recordingTrack() {
    Track track{.id = "vox", .kind = TrackKind::audio, .armed = true};
    track.audioIn = {"interface", {2}};
    return track;
}

std::vector<RecordedTake> recordFor(Rig& rig, Frames frames) {
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
    return takes;
}

}  // namespace

TEST_CASE("fades ramp linearly in and out") {
    CHECK(fadeGain(0, 1000, 100, 200) == 0.0F);
    CHECK(fadeGain(50, 1000, 100, 200) == 0.5F);
    CHECK(fadeGain(100, 1000, 100, 200) == 1.0F);
    CHECK(fadeGain(800, 1000, 100, 200) == 1.0F);
    CHECK(fadeGain(900, 1000, 100, 200) == 0.5F);
    CHECK(fadeGain(500, 1000, 0, 0) == 1.0F);
    CHECK(fadeGain(10, 20, 20, 20) == 0.5F);
}

TEST_CASE("audio clips play with track gain, clip gain and fades on the output channels") {
    Rig rig;
    const TempFolder folder;
    const auto path = folder.path / "ones.wav";
    writeWav(path, std::vector<float>(48000, 1.0F));
    Track track{.id = "pad", .kind = TrackKind::audio, .gain = 0.5F};
    track.audioOut = {"keyboard", {1, 2}};
    track.audioClips.push_back(
        {.path = path, .start = 0, .length = 960, .sourceOffset = 0, .gain = 0.5F, .fadeIn = 240, .fadeOut = 240});
    rig.engine.setSession({.tempo = kTempo, .tracks = {track}});
    playFromStart(rig);
    const Frames start = rig.audio.processedFrames();
    rig.audio.process(30000);

    for (const int channel : {1, 2}) {
        const auto output = rig.audio.output("keyboard", channel);
        const auto at = [&](Frames frame) { return output[static_cast<std::size_t>(start + frame)]; };
        CHECK(at(3000) == Approx(0.125F));
        CHECK(at(12000) == Approx(0.25F));
        CHECK(at(21000) == Approx(0.125F));
        CHECK(at(24000) == 0.0F);
    }

    rig.engine.setGain("pad", 1.0F);
    rig.engine.locate(0);
    rig.audio.process(kBlock);
    REQUIRE(waitFor([&] { return rig.engine.readAheadReady(); }));
    const Frames again = rig.audio.processedFrames();
    rig.audio.process(12800);
    CHECK(rig.audio.output("keyboard", 1)[static_cast<std::size_t>(again + 12000)] == Approx(0.5F));
}

TEST_CASE("an armed audio track records its input to WAV, and the take plays back") {
    Rig rig;
    const TempFolder folder;
    Track track{.id = "vox", .kind = TrackKind::audio, .armed = true};
    track.audioIn = {"interface", {2}};
    track.audioOut = {"interface", {1}};
    rig.engine.setSession({.tempo = kTempo, .tracks = {track}, .takeFolder = folder.path});

    constexpr Frames kLength = 9600;
    std::vector<float> sine(kLength);
    for (std::size_t frame = 0; frame < sine.size(); ++frame) {
        sine[frame] =
            0.5F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(frame) / kRate));
    }
    rig.audio.setInput("interface", 2, sine);
    rig.engine.setRecording(true);
    rig.engine.play();
    rig.audio.process(kLength);
    rig.engine.stop();
    rig.audio.process(kBlock);

    std::vector<RecordedTake> takes;
    REQUIRE(waitFor([&] {
        takes = rig.engine.takeRecordings();
        return !takes.empty();
    }));
    REQUIRE(takes.size() == 1);
    CHECK(takes[0].trackId == "vox");
    CHECK(takes[0].startTick == 0);
    CHECK(readWav(takes[0].audioFile) == sine);

    track.armed = false;
    track.audioClips.push_back({.path = takes[0].audioFile, .start = 0, .length = 384});
    rig.engine.setRecording(false);
    rig.engine.setSession({.tempo = kTempo, .tracks = {track}, .takeFolder = folder.path});
    playFromStart(rig);
    const Frames start = rig.audio.processedFrames();
    rig.audio.process(kLength + kBlock);
    const auto output = rig.audio.output("interface", 1);
    const std::vector<float> played(output.begin() + start, output.begin() + start + kLength);
    CHECK(played == sine);
    CHECK(output[static_cast<std::size_t>(start + kLength)] == 0.0F);
}

TEST_CASE("a missing or wrong rate clip is skipped and reported while the other clips play") {
    Rig rig;
    const TempFolder folder;
    writeWav(folder.path / "ones.wav", std::vector<float>(48000, 1.0F));
    writeWav(folder.path / "slow.wav", std::vector<float>(44100, 1.0F), 44100.0);
    Track track{.id = "pad", .kind = TrackKind::audio};
    track.audioOut = {"keyboard", {1}};
    track.audioClips = {{.path = folder.path / "missing.wav", .start = 0, .length = 960},
                        {.path = folder.path / "slow.wav", .start = 0, .length = 960},
                        {.path = folder.path / "ones.wav", .start = 0, .length = 960}};
    rig.engine.setSession({.tempo = kTempo, .tracks = {track}});

    const auto& errors = rig.engine.sessionErrors();
    REQUIRE(errors.size() == 2);
    CHECK(errors[0].trackId == "pad");
    CHECK(errors[0].message.contains("missing.wav"));
    CHECK(errors[1].message.contains("slow.wav"));
    playFromStart(rig);
    const Frames start = rig.audio.processedFrames();
    rig.audio.process(kBlock);
    CHECK(rig.audio.output("keyboard", 1)[static_cast<std::size_t>(start)] == 1.0F);
}

TEST_CASE("read-ahead is ready again only once the new position is buffered") {
    Rig rig;
    const TempFolder folder;
    constexpr Frames kLength = 192000;
    std::vector<float> ramp(kLength);
    for (std::size_t frame = 0; frame < ramp.size(); ++frame) {
        ramp[frame] = static_cast<float>(frame) / static_cast<float>(kLength);
    }
    writeWav(folder.path / "ramp.wav", ramp);
    Track track{.id = "pad", .kind = TrackKind::audio};
    track.audioOut = {"keyboard", {1}};
    track.audioClips.push_back({.path = folder.path / "ramp.wav", .start = 0, .length = 7680});
    rig.engine.setSession({.tempo = kTempo, .tracks = {track}});
    rig.engine.locate(0);
    rig.audio.process(kBlock);
    REQUIRE(waitFor([&] { return rig.engine.readAheadReady(); }));

    rig.engine.locate(5760);
    CHECK_FALSE(rig.engine.readAheadReady());
    rig.audio.process(kBlock);
    REQUIRE(waitFor([&] { return rig.engine.readAheadReady(); }));
    rig.engine.play();
    const Frames start = rig.audio.processedFrames();
    rig.audio.process(kBlock);

    const auto output = rig.audio.output("keyboard", 1);
    const std::vector<float> played(output.begin() + start, output.begin() + start + kBlock);
    CHECK(played == std::vector<float>(ramp.begin() + 144000, ramp.begin() + 144000 + kBlock));
}

TEST_CASE("a take whose file cannot be created comes back damaged and without a file") {
    Rig rig;
    const TempFolder folder;
    rig.engine.setSession({.tempo = kTempo, .tracks = {recordingTrack()}, .takeFolder = folder.path / "missing"});

    const auto takes = recordFor(rig, 4800);
    REQUIRE(takes.size() == 1);
    CHECK(takes[0].audioFile.empty());
    CHECK_FALSE(takes[0].error.empty());
}

TEST_CASE("a take that overflows the recording buffer comes back damaged") {
    constexpr Frames kHugeBlock = 144000;
    Rig rig{kHugeBlock};
    const TempFolder folder;
    rig.engine.setSession({.tempo = kTempo, .tracks = {recordingTrack()}, .takeFolder = folder.path});

    const auto takes = recordFor(rig, kHugeBlock);
    REQUIRE(takes.size() == 1);
    CHECK(std::filesystem::exists(takes[0].audioFile));
    CHECK(takes[0].error.contains("overflowed"));
}

TEST_CASE("a recorder replaced in the middle of a take still reports the take") {
    Rig rig;
    const TempFolder first;
    const TempFolder second;
    rig.engine.setSession({.tempo = kTempo, .tracks = {recordingTrack()}, .takeFolder = first.path});
    rig.audio.setInput("interface", 2, std::vector<float>(4800, 0.25F));
    rig.engine.setRecording(true);
    rig.engine.play();
    rig.audio.process(4800);

    rig.engine.setSession({.tempo = kTempo, .tracks = {recordingTrack()}, .takeFolder = second.path});
    rig.audio.process(kBlock);
    std::vector<RecordedTake> takes;
    REQUIRE(waitFor([&] {
        takes = rig.engine.takeRecordings();
        return !takes.empty();
    }));
    REQUIRE(takes.size() == 1);
    CHECK(takes[0].audioFile.parent_path() == first.path);
    CHECK(readWav(takes[0].audioFile) == std::vector<float>(4800, 0.25F));
}
