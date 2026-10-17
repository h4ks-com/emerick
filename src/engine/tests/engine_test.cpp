#include "emerick/engine/engine.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <ranges>

#include "rig.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;

namespace {

const emerick::core::TempoMap kTempo{120.0, {4, 4}};
using Bytes = std::vector<std::uint8_t>;

std::vector<Bytes> bytesOf(const std::vector<SentMidi>& sent) {
    return sent | std::views::transform(&SentMidi::bytes) | std::ranges::to<std::vector>();
}

// Runs one block of ones on every input inside open, as a realtime backend starts calling back right away.
class EagerBackend final : public AudioBackend {
public:
    std::vector<AudioDeviceInfo> devices() override { return rigDevices(); }

    StreamLayout open(const StreamRequest& request, AudioCallback callback) override {
        const StreamLayout layout = concatenateDevices(devices(), request);
        const std::vector<float> ones(kBlock, 1.0F);
        const std::vector<const float*> inputs(static_cast<std::size_t>(layout.inputChannels), ones.data());
        std::vector<std::vector<float>> outputs(static_cast<std::size_t>(layout.outputChannels),
                                                std::vector<float>(kBlock));
        const auto pointers = outputs | std::views::transform([](auto& output) { return output.data(); }) |
                              std::ranges::to<std::vector>();
        callback({kBlock, 0, inputs, pointers});
        for (const auto& output : outputs) {
            loudestOpenBlock = std::max(loudestOpenBlock, std::ranges::max(output));
        }
        ++opens;
        return layout;
    }

    void close() override {}

    int opens = 0;
    float loudestOpenBlock = 0.0F;
};

Track midiTrack(std::string id, MidiRoute in, MidiRoute out) {
    Track track{.id = std::move(id), .kind = TrackKind::midi};
    track.midiIn = std::move(in);
    track.midiOut = std::move(out);
    return track;
}

std::vector<SentMidi> waitForSent(const VirtualMidiBackend& midi, const std::string& port, std::size_t count) {
    waitFor([&] { return midi.sent(port).size() >= count; });
    return midi.sent(port);
}

std::vector<RecordedTake> waitForTakes(Engine& engine, std::size_t count) {
    std::vector<RecordedTake> takes;
    waitFor([&] {
        std::ranges::move(engine.takeRecordings(), std::back_inserter(takes));
        return takes.size() >= count;
    });
    std::ranges::sort(takes, {}, &RecordedTake::trackId);
    return takes;
}

}  // namespace

TEST_CASE("transport position advances in frames and ticks and locates by ticks") {
    Rig rig;
    rig.engine.setSession({.tempo = kTempo});
    rig.engine.play();
    rig.audio.process(24000);
    CHECK(rig.engine.position().frames == 24000);
    CHECK(rig.engine.position().ticks == 960);
    CHECK(rig.engine.playing());

    rig.engine.stop();
    rig.engine.locate(1920 + 480);
    rig.audio.process(kBlock);
    CHECK_FALSE(rig.engine.playing());
    CHECK(rig.engine.position().frames == 60000);
    CHECK(rig.engine.position().ticks == 2400);
}

TEST_CASE("clock ports get Start, then a clock every 40 ticks on the exact frame, then Stop") {
    Rig rig;
    rig.engine.setSession({.tempo = kTempo, .clockPorts = {kPort1}});
    rig.engine.play();
    rig.audio.process(48000);
    rig.engine.stop();
    rig.audio.process(kBlock);

    const auto sent = waitForSent(rig.midi, kPort1, 50);
    REQUIRE(sent.size() == 50);
    CHECK(sent.front().bytes == std::vector<std::uint8_t>{0xFA});
    CHECK(frameOf(sent.front()) == 0);
    for (std::size_t pulse = 0; pulse < 48; ++pulse) {
        const SentMidi& clock = sent[pulse + 1];
        CHECK(clock.bytes == std::vector<std::uint8_t>{0xF8});
        CHECK(frameOf(clock) == static_cast<Frames>(pulse) * 1000);
    }
    CHECK(sent.back().bytes == std::vector<std::uint8_t>{0xFC});
    CHECK(frameOf(sent.back()) == 48000);
}

TEST_CASE("MIDI clip events play on the track's port and channel at their frame") {
    Rig rig;
    Track track = midiTrack("keys", {}, {kPort2, 3});
    track.midiClips.push_back({.start = 960, .events = {{0, {0x90, 60, 100}}, {480, {0x80, 60, 0}}}});
    rig.engine.setSession({.tempo = kTempo, .tracks = {track}});
    rig.engine.play();
    rig.audio.process(48000);

    const auto sent = waitForSent(rig.midi, kPort2, 2);
    REQUIRE(sent.size() == 2);
    CHECK(sent[0].bytes == std::vector<std::uint8_t>{0x92, 60, 100});
    CHECK(frameOf(sent[0]) == 24000);
    CHECK(sent[1].bytes == std::vector<std::uint8_t>{0x82, 60, 0});
    CHECK(frameOf(sent[1]) == 36000);

    rig.engine.stop();
    rig.audio.process(kBlock);
    const auto afterStop = waitForSent(rig.midi, kPort2, 3);
    CHECK(afterStop.back().bytes == std::vector<std::uint8_t>{0xB2, 123, 0});
}

TEST_CASE("solo and mute silence the other tracks") {
    Rig rig;
    Track first = midiTrack("first", {}, {kPort1, 1});
    first.midiClips.push_back({.events = {{960, {0x90, 60, 100}}}});
    Track second = midiTrack("second", {}, {kPort2, 2});
    second.midiClips.push_back({.events = {{960, {0x90, 62, 100}}, {2880, {0x90, 64, 100}}}});
    rig.engine.setSession({.tempo = kTempo, .tracks = {first, second}});
    rig.engine.setSolo("second", true);
    rig.engine.play();
    rig.audio.process(48000);
    rig.engine.setMute("second", true);
    rig.audio.process(48000);

    REQUIRE(waitFor([&] { return rig.engine.midiFlushed(); }));
    CHECK(bytesOf(rig.midi.sent(kPort2)) == std::vector<Bytes>{{0x91, 62, 100}, {0x81, 62, 0}});
    CHECK(rig.midi.sent(kPort1).empty());
    CHECK_THROWS_AS(rig.engine.setMute("missing", true), std::invalid_argument);
}

TEST_CASE("MIDI thru echoes input only on tracks with thru set") {
    Rig rig;
    Track echoing = midiTrack("echoing", {kPort1, 1}, {kPort2, 5});
    echoing.midiThru = true;
    const Track silent = midiTrack("silent", {kPort1, 0}, {kPort1, 0});
    rig.engine.setSession({.tempo = kTempo, .tracks = {echoing, silent}});
    rig.audio.process(kBlock);
    const std::vector<std::uint8_t> note{0x90, 64, 90};
    rig.midi.inject(kPort1, note, rig.audio.hostTimeAt(rig.audio.processedFrames()));
    rig.audio.process(kBlock);

    REQUIRE(waitFor([&] { return rig.engine.midiFlushed(); }));
    CHECK(bytesOf(rig.midi.sent(kPort2)) == std::vector<Bytes>{{0x94, 64, 90}});
    CHECK(rig.midi.sent(kPort1).empty());
}

TEST_CASE("mute, solo, locate and stop release the notes a track is sounding") {
    Rig rig;
    Track keys = midiTrack("keys", {}, {kPort2, 1});
    keys.midiClips.push_back({.events = {{0, {0x90, 60, 100}}, {1920, {0x80, 60, 0}}}});
    const Track other = midiTrack("other", {}, {kPort1, 1});
    rig.engine.setSession({.tempo = kTempo, .tracks = {keys, other}});
    rig.engine.play();
    rig.audio.process(4800);

    SECTION("mute") { rig.engine.setMute("keys", true); }
    SECTION("solo on another track") { rig.engine.setSolo("other", true); }
    SECTION("locate") { rig.engine.locate(960); }
    SECTION("stop") { rig.engine.stop(); }
    rig.audio.process(kBlock);

    REQUIRE(waitFor([&] { return rig.engine.midiFlushed(); }));
    const auto sent = bytesOf(rig.midi.sent(kPort2));
    REQUIRE(sent.size() >= 2);
    CHECK(sent[0] == Bytes{0x90, 60, 100});
    CHECK(sent[1] == Bytes{0x80, 60, 0});
}

TEST_CASE("each port sends its messages in timestamp order") {
    const auto start = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    Rig rig{kBlock, std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count()};
    Track keys = midiTrack("keys", {}, {kPort1, 1});
    keys.midiClips.push_back({.events = {{39, {0x90, 60, 100}}, {41, {0x80, 60, 0}}}});
    rig.engine.setSession({.tempo = kTempo, .tracks = {keys}, .clockPorts = {kPort1}});
    rig.engine.play();
    rig.audio.process(1100);

    REQUIRE(waitFor([&] { return rig.engine.midiFlushed(); }));
    const auto sent = rig.midi.sent(kPort1);
    CHECK(bytesOf(sent) == std::vector<Bytes>{{0xFA}, {0xF8}, {0x90, 60, 100}, {0xF8}, {0x80, 60, 0}});
    CHECK(std::ranges::is_sorted(sent, {}, &SentMidi::timestampNs));
}

TEST_CASE("a reopened stream drops the snapshot built for the old devices") {
    auto owner = std::make_unique<EagerBackend>();
    const EagerBackend& audio = *owner;
    Engine engine(std::move(owner),
                  std::make_unique<VirtualMidiBackend>(std::vector<std::string>{}, std::vector<std::string>{}),
                  {kRate, kBlock});
    Track monitored{.id = "vox", .kind = TrackKind::audio, .monitor = true};
    monitored.audioIn = {"interface", {1}};
    monitored.audioOut = {"interface", {1}};
    engine.setSession({.tempo = kTempo, .tracks = {monitored}});

    Track moved = monitored;
    moved.audioIn = {"keyboard", {1}};
    moved.audioOut = {"keyboard", {1}};
    engine.setSession({.tempo = kTempo, .tracks = {moved}});

    CHECK(audio.opens == 2);
    CHECK(audio.loudestOpenBlock == 0.0F);
}

TEST_CASE("armed MIDI tracks record their channel, and sysex goes to the claiming track") {
    Rig rig;
    auto armed = [](std::string id, int channel) {
        Track track = midiTrack(std::move(id), {kPort1, channel}, {});
        track.armed = true;
        return track;
    };
    Track chordLane = armed("c-chords", 0);
    Track unarmed = armed("d-unarmed", 1);
    unarmed.armed = false;
    const std::vector<std::uint8_t> chord{0xF0, 0x43, 0x7E, 0x02, 0x00, 0x00, 0x7F, 0x7F, 0xF7};

    SECTION("a claim routes sysex to the claiming track only") {
        chordLane.claimsSysex = true;
        rig.engine.setSession(
            {.tempo = kTempo, .tracks = {armed("a-right", 1), armed("b-left", 2), chordLane, unarmed}});
        rig.engine.setRecording(true);
        rig.engine.play();
        rig.audio.process(kBlock);
        rig.midi.inject(kPort1, std::vector<std::uint8_t>{0x90, 60, 100}, rig.audio.hostTimeAt(74));
        rig.midi.inject(kPort1, std::vector<std::uint8_t>{0x91, 48, 100}, rig.audio.hostTimeAt(80));
        rig.midi.inject(kPort1, chord, rig.audio.hostTimeAt(90));
        rig.audio.process(kBlock * 10);
        rig.engine.stop();
        rig.audio.process(kBlock);

        const auto takes = waitForTakes(rig.engine, 3);
        REQUIRE(takes.size() == 3);
        CHECK(takes[0].trackId == "a-right");
        CHECK(takes[0].midiEvents == std::vector<MidiEvent>{{3, {0x90, 60, 100}}});
        CHECK(takes[1].trackId == "b-left");
        CHECK(takes[1].midiEvents == std::vector<MidiEvent>{{3, {0x91, 48, 100}}});
        CHECK(takes[2].trackId == "c-chords");
        CHECK(takes[2].midiEvents == std::vector<MidiEvent>{{3, {0x90, 60, 100}}, {3, {0x91, 48, 100}}, {4, chord}});
    }

    SECTION("with no claim, sysex goes to the first armed track on the port") {
        rig.engine.setSession({.tempo = kTempo, .tracks = {armed("a-right", 1), armed("b-left", 2), chordLane}});
        rig.engine.setRecording(true);
        rig.engine.play();
        rig.audio.process(kBlock);
        rig.midi.inject(kPort1, chord, rig.audio.hostTimeAt(64));
        rig.audio.process(kBlock);
        rig.engine.stop();
        rig.audio.process(kBlock);

        const auto takes = waitForTakes(rig.engine, 1);
        REQUIRE(takes.size() == 1);
        CHECK(takes[0].trackId == "a-right");
        CHECK(takes[0].startTick == 0);
        CHECK(takes[0].midiEvents == std::vector<MidiEvent>{{3, chord}});
    }
}

TEST_CASE("a session swapped in during playback takes over at the next block") {
    Rig rig;
    Track before = midiTrack("keys", {}, {kPort1, 1});
    before.midiClips.push_back({.events = {{0, {0x90, 60, 100}}, {1920, {0x90, 60, 100}}}});
    rig.engine.setSession({.tempo = kTempo, .tracks = {before}});
    rig.engine.play();
    rig.audio.process(24000);

    Track after = before;
    after.midiClips = {{.events = {{1920, {0x90, 61, 100}}, {2880, {0x90, 62, 100}}}}};
    rig.engine.setSession({.tempo = kTempo, .tracks = {after}});
    rig.audio.process(72000);

    const auto sent = waitForSent(rig.midi, kPort1, 3);
    REQUIRE(sent.size() == 3);
    CHECK(sent[0].bytes[1] == 60);
    CHECK(frameOf(sent[0]) == 0);
    CHECK(sent[1].bytes[1] == 61);
    CHECK(frameOf(sent[1]) == 48000);
    CHECK(sent[2].bytes[1] == 62);
    CHECK(frameOf(sent[2]) == 72000);
    CHECK(rig.engine.position().frames == 96000);
}
