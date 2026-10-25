#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <ranges>

#include "emerick/engine/engine.hpp"
#include "rig.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;

namespace {

const emerick::core::TempoMap kTempo{120.0, {4, 4}};
using Bytes = std::vector<std::uint8_t>;

std::vector<Bytes> sentBytes(const Rig& rig, const std::string& port) {
    REQUIRE(waitFor([&] { return rig.engine.midiFlushed(); }));
    return rig.midi.sent(port) | std::views::transform(&SentMidi::bytes) | std::ranges::to<std::vector>();
}

// Bank, program, volume and sustain on channel 1, then a bend, all before bar 2, and later changes after it.
Track pianoTrack(MidiRoute out) {
    Track track{.id = "piano", .kind = TrackKind::midi};
    track.midiOut = std::move(out);
    track.midiClips.push_back({.start = 0,
                               .events = {{0, {0xC0, 5}},
                                          {0, {0xB0, 0, 8}},
                                          {0, {0xB0, 32, 1}},
                                          {10, {0xB0, 7, 90}},
                                          {20, {0xB0, 7, 100}},
                                          {30, {0xB0, 64, 127}},
                                          {40, {0x90, 60, 100}},
                                          {50, {0xE0, 0, 80}},
                                          {60, {0xB0, 6, 2}},
                                          {1920, {0xB0, 7, 20}},
                                          {1920, {0xC0, 9}}}});
    return track;
}

const std::vector<Bytes> kChasedAtBar2{{0xB0, 0, 8},   {0xB0, 32, 1},   {0xC0, 5},
                                       {0xB0, 7, 100}, {0xB0, 64, 127}, {0xE0, 0, 80}};

void clearSent(Rig& rig) {
    REQUIRE(waitFor([&] { return rig.engine.midiFlushed(); }));
    rig.midi.clearSent();
}

}  // namespace

TEST_CASE("play chases bank, program, controllers and pitch bend before the playhead, ahead of Start") {
    Rig rig;
    rig.engine.setSession({.tempo = kTempo, .tracks = {pianoTrack({kPort1, 0})}, .clockPorts = {kPort1}});
    rig.engine.locate(1920);
    rig.engine.play();
    rig.audio.process(kBlock);

    std::vector<Bytes> expected = kChasedAtBar2;
    expected.push_back({0xB0, 7, 20});
    expected.push_back({0xC0, 9});
    expected.push_back({0xFA});
    const auto sent = sentBytes(rig, kPort1);
    REQUIRE(sent.size() >= expected.size());
    CHECK(std::vector(sent.begin(), sent.begin() + static_cast<std::ptrdiff_t>(expected.size())) == expected);
}

TEST_CASE("a chase follows the track's output channel and a locate while playing") {
    Rig rig;
    rig.engine.setSession({.tempo = kTempo, .tracks = {pianoTrack({kPort1, 3})}});
    rig.engine.play();
    rig.audio.process(kBlock);
    clearSent(rig);
    rig.engine.locate(1000);
    rig.audio.process(kBlock);

    CHECK(sentBytes(rig, kPort1) ==
          std::vector<Bytes>{{0xB2, 0, 8}, {0xB2, 32, 1}, {0xC2, 5}, {0xB2, 7, 100}, {0xB2, 64, 127}, {0xE2, 0, 80}});
}

TEST_CASE("a muted track and a session that does not chase send nothing before Start") {
    Rig rig;
    Session session{.tempo = kTempo, .tracks = {pianoTrack({kPort1, 0})}};
    SECTION("muted") { session.tracks[0].mute = true; }
    SECTION("chase off") { session.chaseMidi = false; }
    rig.engine.setSession(session);
    rig.engine.locate(1000);
    rig.engine.play();
    rig.audio.process(kBlock);

    CHECK(sentBytes(rig, kPort1).empty());
}

TEST_CASE("sent MIDI goes out before Start and only on ports the session uses") {
    const auto start = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    Rig rig{kBlock, std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count()};
    Track listening{.id = "listening", .kind = TrackKind::midi};
    listening.midiIn = {kPort2, 0};
    rig.engine.setSession({.tempo = kTempo, .tracks = {listening}, .clockPorts = {kPort1}});
    REQUIRE(rig.engine.streamOpen());
    const Bytes chord{0xF0, 0x43, 0x7E, 0x02, 0x00, 0x00, 0x7F, 0x7F, 0xF7};
    rig.engine.sendMidi(kPort1, chord);
    rig.engine.sendMidi(kPort2, Bytes{0x90, 60, 100});
    rig.engine.play();
    rig.audio.process(kBlock);

    const auto sent = sentBytes(rig, kPort1);
    REQUIRE(sent.size() >= 2);
    CHECK(sent[0] == chord);
    CHECK(sent[1] == Bytes{0xFA});
    CHECK(sentBytes(rig, kPort2).empty());
    CHECK_THROWS_AS(rig.engine.sendMidi(kPort1, Bytes(std::size_t{1} << 16)), std::invalid_argument);
}

TEST_CASE("panic stops the clock ports and silences every channel of every output port while stopped") {
    Rig rig;
    Track keys{.id = "keys", .kind = TrackKind::midi};
    keys.midiOut = {kPort2, 1};
    keys.midiClips.push_back({.events = {{0, {0x90, 60, 100}}}});
    rig.engine.setSession({.tempo = kTempo, .tracks = {keys}, .clockPorts = {kPort1}});
    rig.engine.play();
    rig.audio.process(kBlock);
    rig.engine.stop();
    rig.audio.process(kBlock);
    clearSent(rig);
    rig.engine.panic();
    rig.audio.process(kBlock);

    std::vector<Bytes> silence;
    for (std::uint8_t channel = 0; channel < 16; ++channel) {
        for (const std::uint8_t controller : Bytes{64, 120, 123}) {
            silence.push_back({static_cast<std::uint8_t>(0xB0 | channel), controller, 0});
        }
    }
    std::vector<Bytes> clockPort{{0xFC}};
    clockPort.insert(clockPort.end(), silence.begin(), silence.end());
    CHECK(sentBytes(rig, kPort1) == clockPort);
    CHECK(sentBytes(rig, kPort2) == silence);
}

TEST_CASE("panic releases the notes a playing track holds") {
    Rig rig;
    Track keys{.id = "keys", .kind = TrackKind::midi};
    keys.midiOut = {kPort2, 1};
    keys.midiClips.push_back({.events = {{0, {0x90, 60, 100}}}});
    rig.engine.setSession({.tempo = kTempo, .tracks = {keys}});
    rig.engine.play();
    rig.audio.process(kBlock);
    rig.engine.panic();
    rig.audio.process(kBlock);

    const auto sent = sentBytes(rig, kPort2);
    REQUIRE(sent.size() >= 2);
    CHECK(sent[1] == Bytes{0x80, 60, 0});
}

TEST_CASE("clock ports get clock while stopped only when the session asks for it") {
    Rig rig;
    const bool whileStopped = GENERATE(true, false);
    rig.engine.setSession({.tempo = kTempo, .clockPorts = {kPort1}, .clockWhileStopped = whileStopped});
    rig.audio.process(4800);

    const auto sent = sentBytes(rig, kPort1);
    CHECK(sent.size() == (whileStopped ? 5U : 0U));
    CHECK(std::ranges::all_of(sent, [](const Bytes& bytes) { return bytes == Bytes{0xF8}; }));
}
