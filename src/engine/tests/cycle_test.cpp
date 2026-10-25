#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <ranges>
#include <vector>

#include "emerick/engine/engine.hpp"
#include "rig.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;
using Catch::Approx;

namespace {

// At 120 BPM a beat of 960 ticks lasts 24000 frames, so the cycle runs from frame 12000 to frame 48000.
const emerick::core::TempoMap kTempo{120.0, {4, 4}};
constexpr Ticks kCycleStart = 480;
constexpr Ticks kCycleEnd = 1920;
constexpr Frames kStartFrame = 12000;
constexpr Frames kEndFrame = 48000;
constexpr float kRampStep = 1e-5F;
using Bytes = std::vector<std::uint8_t>;
const Bytes kRestart{0xF0, 0x43, 0x7E, 0x00, 0x09, 0x7F, 0xF7};

Cycle cycle() { return {.start = kCycleStart, .end = kCycleEnd, .restart = {{kPort1, kRestart}}}; }

Track keysTrack() {
    Track track{.id = "keys", .kind = TrackKind::midi};
    track.midiOut = {kPort2, 1};
    track.midiClips.push_back({.start = 960, .events = {{0, {0x90, 60, 100}}, {1440, {0x80, 60, 0}}}});
    return track;
}

std::vector<SentMidi> sent(const Rig& rig, const std::string& port) {
    REQUIRE(waitFor([&] { return rig.engine.midiFlushed(); }));
    return rig.midi.sent(port);
}

bool isClock(const SentMidi& message) { return message.bytes == Bytes{0xF8}; }

}  // namespace

TEST_CASE("playback jumps from the cycle end to its start on the exact frame, mid-block") {
    Rig rig;
    rig.engine.setSession({.tempo = kTempo, .cycle = cycle()});
    rig.engine.play();
    rig.audio.process(kEndFrame + 1000);
    CHECK(rig.engine.position().frames == kStartFrame + 1000);
    rig.audio.process(kEndFrame - kStartFrame);
    CHECK(rig.engine.position().frames == kStartFrame + 1000);
}

TEST_CASE("each jump sends Stop, releases held notes, sends the restart messages and Start, and the clock goes on") {
    Rig rig;
    rig.engine.setSession({.tempo = kTempo, .tracks = {keysTrack()}, .clockPorts = {kPort1}, .cycle = cycle()});
    rig.engine.play();
    rig.audio.process(kEndFrame + 24000);

    const auto control = sent(rig, kPort1) |
                         std::views::filter([](const SentMidi& message) { return !isClock(message); }) |
                         std::ranges::to<std::vector>();
    REQUIRE(control.size() == 4);
    CHECK(control[0].bytes == Bytes{0xFA});
    CHECK(control[1].bytes == Bytes{0xFC});
    CHECK(control[2].bytes == kRestart);
    CHECK(control[3].bytes == Bytes{0xFA});
    for (const SentMidi& message : std::span(control).subspan(1)) {
        CHECK(frameOf(message) == kEndFrame);
    }
    const auto clocks = sent(rig, kPort1) | std::views::filter(isClock) | std::ranges::to<std::vector>();
    CHECK(std::ranges::count(clocks, kEndFrame, frameOf) == 1);
    CHECK(std::ranges::count(clocks, kEndFrame - 1000, frameOf) == 1);

    const auto keys = sent(rig, kPort2);
    REQUIRE(keys.size() == 3);
    CHECK(keys[0].bytes == Bytes{0x90, 60, 100});
    CHECK(frameOf(keys[0]) == 24000);
    CHECK(keys[1].bytes == Bytes{0x80, 60, 0});
    CHECK(frameOf(keys[1]) == kEndFrame);
    CHECK(keys[2].bytes == Bytes{0x90, 60, 100});
    CHECK(frameOf(keys[2]) == kEndFrame + 24000 - kStartFrame);
}

TEST_CASE("audio plays the cycle start right after the jump while the stream reads ahead again") {
    const TempFolder folder;
    const auto ramp = folder.path / "ramp.wav";
    writeWav(ramp, std::views::iota(0, 96000) | std::views::transform([](int frame) {
                       return static_cast<float>(frame) * kRampStep;
                   }) | std::ranges::to<std::vector>());
    Track track{.id = "loop", .kind = TrackKind::audio};
    track.audioOut = {"keyboard", {1}};
    track.audioClips.push_back({.path = ramp, .start = 0, .length = 3840, .sourceOffset = 0, .gain = 1.0F});
    Rig rig;
    rig.engine.setSession({.tempo = kTempo, .tracks = {track}, .cycle = cycle()});
    playOnceReadAhead(rig);
    const Frames played = rig.audio.processedFrames();
    rig.audio.process(kEndFrame + 4000);

    const auto output = rig.audio.output("keyboard", 1);
    const auto sampleAt = [&](Frames timeline) { return output[static_cast<std::size_t>(played + timeline)]; };
    CHECK(sampleAt(kEndFrame - 1) == Approx(static_cast<float>(kEndFrame - 1) * kRampStep));
    for (Frames after = 0; after < 4000; after += 97) {
        CHECK(sampleAt(kEndFrame + after) == Approx(static_cast<float>(kStartFrame + after) * kRampStep));
    }

    REQUIRE(waitFor([&] { return rig.engine.readAheadReady(); }));
    rig.audio.process(20000);
    const auto later = rig.audio.output("keyboard", 1);
    for (Frames after = 4000; after < 24000; after += 997) {
        CHECK(later[static_cast<std::size_t>(played + kEndFrame + after)] ==
              Approx(static_cast<float>(kStartFrame + after) * kRampStep));
    }
}

TEST_CASE("recording and a playhead past the cycle end play straight on") {
    Rig rig;
    rig.engine.setSession({.tempo = kTempo, .cycle = cycle()});
    SECTION("recording") {
        rig.engine.setRecording(true);
        rig.engine.play();
        rig.audio.process(kEndFrame + 1000);
        CHECK(rig.engine.position().frames == kEndFrame + 1000);
    }
    SECTION("past the end") {
        rig.engine.locate(kCycleEnd);
        rig.engine.play();
        rig.audio.process(1000);
        CHECK(rig.engine.position().frames == kEndFrame + 1000);
    }
}
