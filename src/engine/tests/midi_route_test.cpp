#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <ranges>
#include <vector>

#include "emerick/engine/clap_instrument.hpp"
#include "rig.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;

namespace {

thread_local bool countingAllocations = false;
thread_local std::size_t allocations = 0;

}  // namespace

// We count the allocations a thread makes while it counts, so a test can prove a realtime path never allocates.
void* operator new(std::size_t size) {
    if (countingAllocations) {
        ++allocations;
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept { std::free(memory); }

void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

using Bytes = std::vector<std::uint8_t>;

const emerick::core::TempoMap kTempo{120.0, {4, 4}};
constexpr Frames kBeatFrames = 24000;
const Bytes kSysex{0xF0, 0x43, 0x7E, 0x02, 0x10, 0x00, 0xF7};

std::shared_ptr<ClapInstrument> sine() {
    auto created = ClapInstrument::create(EMERICK_TEST_SINE_CLAP, "org.emerick.test.sine");
    REQUIRE(created.has_value());
    return *created;
}

Track device(std::string id) {
    Track track{.id = std::move(id), .kind = TrackKind::midi, .monitor = true};
    track.audioOut = {"interface", {1, 2}};
    track.instrument = sine();
    return track;
}

// A track that takes the device's MIDI and sends it out of port 2 as it comes.
Track thruFrom(const std::string& source, int channel = 0) {
    Track track{.id = "thru", .kind = TrackKind::midi};
    track.midiIn = {.portId = {}, .channel = channel};
    track.midiInTrack = source;
    track.midiOut = {kPort2, 0};
    track.midiThru = true;
    return track;
}

std::vector<Bytes> bytesOf(std::span<const InstrumentMidi> produced) {
    return produced | std::views::transform([](const InstrumentMidi& message) {
               return Bytes(message.bytes.begin(), message.bytes.end());
           }) |
           std::ranges::to<std::vector>();
}

std::vector<Bytes> bytesOf(const std::vector<SentMidi>& sent) {
    return sent | std::views::transform(&SentMidi::bytes) | std::ranges::to<std::vector>();
}

struct Buffers {
    std::vector<float> left = std::vector<float>(512);
    std::vector<float> right = std::vector<float>(512);
    std::array<float*, 2> mix{left.data(), right.data()};
};

}  // namespace

TEST_CASE("a sysex reaches an instrument intact, and what it sends comes back as MIDI") {
    const std::shared_ptr<ClapInstrument> instrument = sine();
    CHECK(instrument->producesMidi());
    REQUIRE(instrument->activate(kRate, 512));
    Buffers buffers;
    instrument->queueMidi(kSysex, 0);
    instrument->queueMidi(Bytes{0x91, 60, 100}, 0);
    instrument->processBlock(buffers.mix, 512, 0);
    CHECK(bytesOf(instrument->producedMidi()) == std::vector<Bytes>{kSysex, {0x91, 60, 100}});
    instrument->processBlock(buffers.mix, 512, 0);
    CHECK(instrument->producedMidi().empty());
}

TEST_CASE("queueing MIDI and sysex and processing an instrument never allocate") {
    const std::shared_ptr<ClapInstrument> instrument = sine();
    REQUIRE(instrument->activate(kRate, 512));
    Buffers buffers;
    instrument->processBlock(buffers.mix, 512, 0);
    countingAllocations = true;
    instrument->queueMidi(kSysex, 0);
    instrument->queueMidi(std::array<std::uint8_t, 3>{0x90, 60, 100}, 0);
    instrument->processBlock(buffers.mix, 512, 0);
    const std::size_t produced = instrument->producedMidi().size();
    countingAllocations = false;
    CHECK(produced == 2);
    CHECK(allocations == 0);
}

TEST_CASE("live sysex reaches a device, and its output reaches routed tracks through a chain in the same block") {
    Rig rig;
    Track source = device("source");
    source.midiIn = {kPort1, 0};
    source.claimsSysex = true;
    Track middle = device("middle");
    middle.midiInTrack = "source";
    // The routed tracks come first, so the engine has to order the instruments itself.
    rig.engine.setSession({.tempo = kTempo, .tracks = {thruFrom("middle"), middle, source}});
    rig.audio.process(kBlock);
    const std::int64_t sentAt = rig.audio.hostTimeAt(rig.audio.processedFrames());
    rig.midi.inject(kPort1, kSysex, sentAt);
    rig.midi.inject(kPort1, Bytes{0x90, 64, 90}, sentAt);
    rig.audio.process(2 * kBlock);

    REQUIRE(waitFor([&] { return rig.engine.midiFlushed(); }));
    const std::vector<SentMidi> sent = rig.midi.sent(kPort2);
    CHECK(bytesOf(sent) == std::vector<Bytes>{kSysex, {0x90, 64, 90}});
    CHECK(std::ranges::all_of(sent, [&](const SentMidi& message) { return message.timestampNs == sentAt; }));
}

TEST_CASE("a sysex and notes in a clip reach the device, and its output reaches the routed track") {
    Rig rig;
    Track source = device("source");
    source.midiClips.push_back({.start = 0, .events = {{0, kSysex}, {480, {0x90, 60, 100}}, {720, {0x80, 60, 0}}}});
    rig.engine.setSession({.tempo = kTempo, .tracks = {source, thruFrom("source")}});
    rig.engine.play();
    rig.audio.process(kBeatFrames);

    REQUIRE(waitFor([&] { return rig.engine.midiFlushed(); }));
    CHECK(bytesOf(rig.midi.sent(kPort2)) == std::vector<Bytes>{kSysex, {0x90, 60, 100}, {0x80, 60, 0}});
}

TEST_CASE("a track that takes one channel of a device gets only that channel") {
    Rig rig;
    Track source = device("source");
    source.midiClips.push_back(
        {.start = 0, .events = {{0, kSysex}, {0, {0x90, 60, 100}}, {0, {0x91, 62, 100}}, {480, {0x81, 62, 0}}}});
    rig.engine.setSession({.tempo = kTempo, .tracks = {source, thruFrom("source", 2)}});
    rig.engine.play();
    rig.audio.process(kBeatFrames);

    REQUIRE(waitFor([&] { return rig.engine.midiFlushed(); }));
    CHECK(bytesOf(rig.midi.sent(kPort2)) == std::vector<Bytes>{{0x91, 62, 100}, {0x81, 62, 0}});
}

TEST_CASE("recording a track that takes a device's MIDI records what the device sends") {
    Rig rig;
    Track source = device("source");
    source.midiClips.push_back({.start = 0, .events = {{0, kSysex}, {480, {0x90, 60, 100}}, {720, {0x80, 60, 0}}}});
    Track recorded{.id = "recorded", .kind = TrackKind::midi, .armed = true};
    recorded.midiInTrack = "source";
    rig.engine.setSession({.tempo = kTempo, .tracks = {recorded, source}});
    rig.engine.setRecording(true);
    rig.engine.play();
    rig.audio.process(kBeatFrames);
    rig.engine.stop();
    rig.audio.process(kBlock);
    std::vector<RecordedTake> takes;
    REQUIRE(waitFor([&] {
        takes = rig.engine.takeRecordings();
        return !takes.empty();
    }));

    REQUIRE(takes.size() == 1);
    CHECK(takes[0].trackId == "recorded");
    CHECK(takes[0].midiEvents == std::vector<MidiEvent>{{0, kSysex}, {480, {0x90, 60, 100}}, {720, {0x80, 60, 0}}});
}
