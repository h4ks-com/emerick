#include <sndfile.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <limits>

#include "emerick/engine/engine.hpp"
#include "emerick/engine/sound_file.hpp"
#include "rig.hpp"
#include "take_recorder.hpp"

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

Track recordingTrack() {
    Track track{.id = "vox", .kind = TrackKind::audio, .armed = true};
    track.audioIn = {"interface", {2}};
    return track;
}

std::vector<RecordedTake> waitForTakes(Rig& rig) {
    std::vector<RecordedTake> takes;
    REQUIRE(waitFor([&] {
        takes = rig.engine.takeRecordings();
        return !takes.empty();
    }));
    return takes;
}

}  // namespace

TEST_CASE("a take keeps its length and timing when the writer falls behind, and counts the frames it lost") {
    // At 1 kHz the ring holds about four blocks of 500 frames, and only our poll drains it, as a stalled writer would.
    constexpr double kSlowRate = 1000.0;
    constexpr Frames kFrames = 500;
    const TempFolder folder;
    TakeInbox inbox;
    {
        TakeRecorder recorder("vox", TrackKind::audio, 1, kSlowRate, kFrames, 0, folder.path, inbox);
        recorder.begin(0, 0);
        std::vector<float> block(static_cast<std::size_t>(kFrames));
        const std::array<const float*, 1> channels{block.data()};
        for (int number = 1; number <= 7; ++number) {
            std::ranges::fill(block, static_cast<float>(number));
            recorder.writeAudio(channels, kFrames);
            if (number == 6) {
                recorder.poll();
            }
        }
        recorder.end();
        recorder.poll();
    }

    const std::vector<RecordedTake> takes = inbox.take();
    REQUIRE(takes.size() == 1);
    const std::vector<float> recorded = readWav(takes[0].audioFile);
    REQUIRE(recorded.size() == static_cast<std::size_t>(7 * kFrames));
    CHECK(takes[0].droppedFrames == 2 * kFrames);
    CHECK(takes[0].error.empty());
    for (int number = 1; number <= 7; ++number) {
        INFO(number);
        const float expected = number == 5 || number == 6 ? 0.0F : static_cast<float>(number);
        CHECK(recorded[static_cast<std::size_t>((number - 1) * kFrames)] == expected);
        CHECK(recorded[static_cast<std::size_t>(number * kFrames - 1)] == expected);
    }
}

TEST_CASE("a take whose block never fits the ring is silence of the exact length") {
    constexpr Frames kHugeBlock = 144000;
    Rig rig{kHugeBlock};
    const TempFolder folder;
    rig.engine.setSession({.tempo = kTempo, .tracks = {recordingTrack()}, .takeFolder = folder.path});
    rig.audio.setInput("interface", 2, std::vector<float>(kHugeBlock, 0.5F));
    rig.engine.setRecording(true);
    rig.engine.play();
    rig.audio.process(kHugeBlock);
    rig.engine.stop();
    rig.audio.process(kBlock);

    const auto takes = waitForTakes(rig);
    REQUIRE(takes.size() == 1);
    CHECK(takes[0].droppedFrames == kHugeBlock);
    CHECK(readWav(takes[0].audioFile) == std::vector<float>(kHugeBlock, 0.0F));
}

TEST_CASE("a take records NaN and infinite input as zeros and counts them") {
    Rig rig;
    const TempFolder folder;
    rig.engine.setSession({.tempo = kTempo, .tracks = {recordingTrack()}, .takeFolder = folder.path});
    constexpr float kInfinity = std::numeric_limits<float>::infinity();
    rig.audio.setInput("interface", 2, {0.5F, std::numeric_limits<float>::quiet_NaN(), kInfinity, -kInfinity, 0.25F});
    rig.engine.setRecording(true);
    rig.engine.play();
    rig.audio.process(kBlock);
    rig.engine.stop();
    rig.audio.process(kBlock);

    const auto takes = waitForTakes(rig);
    REQUIRE(takes.size() == 1);
    CHECK(takes[0].nonFiniteSamples == 3);
    const std::vector<float> recorded = readWav(takes[0].audioFile);
    REQUIRE(recorded.size() == static_cast<std::size_t>(kBlock));
    CHECK(std::vector<float>(recorded.begin(), recorded.begin() + 5) ==
          std::vector<float>{0.5F, 0.0F, 0.0F, 0.0F, 0.25F});
}

TEST_CASE("a MIDI take counts the events a full ring turned away") {
    const TempFolder folder;
    TakeInbox inbox;
    {
        TakeRecorder recorder("keys", TrackKind::midi, 0, kRate, kBlock, 0, folder.path, inbox);
        recorder.begin(0, 0);
        const std::array<std::uint8_t, 3> note{0x90, 60, 100};
        for (int event = 0; event < 20000; ++event) {
            recorder.writeMidi(event, note);
        }
        recorder.countLostMidi(5);
        recorder.end();
        recorder.poll();
    }

    const std::vector<RecordedTake> takes = inbox.take();
    REQUIRE(takes.size() == 1);
    CHECK(takes[0].droppedMidiEvents > 5);
    CHECK(takes[0].midiEvents.size() + takes[0].droppedMidiEvents == 20005);
}

TEST_CASE("a stream that stops calling back stops the transport once, keeps the take and recovers") {
    using std::chrono::seconds;
    Rig rig;
    const TempFolder folder;
    rig.engine.setSession({.tempo = kTempo, .tracks = {recordingTrack()}, .takeFolder = folder.path});
    rig.audio.setInput("interface", 2, std::vector<float>(4800, 0.25F));
    rig.engine.setRecording(true);
    rig.engine.play();
    rig.audio.process(4800);
    const auto start = std::chrono::steady_clock::time_point{} + seconds{100};

    CHECK_FALSE(rig.engine.watchStream(start));
    CHECK_FALSE(rig.engine.watchStream(start + seconds{1}));
    CHECK(rig.engine.watchStream(start + kStreamStall));
    CHECK_FALSE(rig.engine.watchStream(start + seconds{5}));
    CHECK(rig.engine.streamStalled());
    CHECK_FALSE(rig.engine.playing());
    CHECK_FALSE(rig.audio.isOpen());
    const auto takes = waitForTakes(rig);
    REQUIRE(takes.size() == 1);
    CHECK(readWav(takes[0].audioFile) == std::vector<float>(4800, 0.25F));

    rig.engine.setRecording(false);
    rig.engine.play();
    CHECK_FALSE(rig.engine.watchStream(start + seconds{6}));
    CHECK(rig.engine.streamStalled());
    rig.audio.process(kBlock);
    CHECK_FALSE(rig.engine.watchStream(start + seconds{7}));
    CHECK_FALSE(rig.engine.streamStalled());
    CHECK(rig.engine.playing());
}
