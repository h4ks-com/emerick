#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <ranges>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

#include "emerick/engine/plugin.hpp"
#include "emerick/engine/plugin_scan.hpp"
#include "rig.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;

namespace {

const emerick::core::TempoMap kTempo{120.0, {4, 4}};
constexpr Frames kBeatFrames = 24000;

std::shared_ptr<Plugin> sine() {
    auto created = createClapPlugin(EMERICK_TEST_SINE_CLAP, "org.emerick.test.sine");
    REQUIRE(created.has_value());
    return *created;
}

Track synthTrack(std::shared_ptr<Plugin> instrument) {
    Track track{.id = "synth", .kind = TrackKind::midi};
    track.audioOut = {"interface", {1, 2}};
    track.instrument = std::move(instrument);
    return track;
}

float loudest(std::span<const float> samples) {
    return std::ranges::max(samples | std::views::transform([](float sample) { return std::abs(sample); }));
}

std::vector<std::uint8_t> levelState(float level) {
    const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(float)>>(level);
    return {bytes.begin(), bytes.end()};
}

}  // namespace

TEST_CASE("a CLAP bundle lists its plugins with their features") {
    const auto described = describeClapBundle(EMERICK_TEST_SINE_CLAP);
    REQUIRE(described.has_value());
    REQUIRE(described->size() == 1);
    CHECK(described->front().id == "org.emerick.test.sine");
    CHECK(described->front().name == "Test Sine");
    CHECK(described->front().features == std::vector<std::string>{"instrument", "synthesizer"});
}

TEST_CASE("a file that is no CLAP bundle reports why") {
    const auto described = describeClapBundle("/no/such/plugin.clap");
    REQUIRE_FALSE(described.has_value());
    CHECK(described.error().find("/no/such/plugin.clap") != std::string::npos);
    CHECK_FALSE(createClapPlugin(EMERICK_TEST_SINE_CLAP, "org.emerick.missing").has_value());
}

TEST_CASE("a note in a MIDI clip plays through the instrument into the track's output, and stops at its note-off") {
    Rig rig;
    Track track = synthTrack(sine());
    track.midiClips.push_back({.start = 0, .events = {{0, {0x90, 69, 100}}, {480, {0x80, 69, 0}}}});
    rig.engine.setSession({.tempo = kTempo, .tracks = {track}});
    rig.engine.play();
    rig.audio.process(kBeatFrames);
    const std::vector<float> left = rig.audio.output("interface", 1);
    const std::vector<float> right = rig.audio.output("interface", 2);
    const auto during = std::span(left).subspan(kBlock, kBeatFrames / 2 - 2 * kBlock);
    CHECK(loudest(during) > 0.4F);
    CHECK(loudest(during) <= 0.5F);
    CHECK(loudest(std::span(right).subspan(kBlock, kBlock)) > 0.0F);
    CHECK(loudest(std::span(left).subspan(kBeatFrames / 2 + kBlock)) == 0.0F);
}

TEST_CASE("live MIDI plays a monitored instrument while the transport stops") {
    Rig rig;
    Track track = synthTrack(sine());
    track.midiIn = {kPort1, 0};
    track.monitor = true;
    rig.engine.setSession({.tempo = kTempo, .tracks = {track}});
    rig.audio.process(kBlock);
    const std::array<std::uint8_t, 3> noteOn{0x90, 60, 100};
    rig.midi.inject(kPort1, noteOn, rig.audio.hostTimeAt(rig.audio.processedFrames()));
    rig.audio.process(4 * kBlock);
    const std::vector<float> left = rig.audio.output("interface", 1);
    CHECK(loudest(std::span(left).last(2 * kBlock)) > 0.4F);
}

TEST_CASE("an instrument saves and loads its state, and plays as the loaded state says") {
    Rig rig;
    const std::shared_ptr<Plugin> instrument = sine();
    REQUIRE(instrument->loadState(levelState(0.25F)));
    CHECK(instrument->saveState() == levelState(0.25F));
    Track track = synthTrack(instrument);
    track.midiClips.push_back({.start = 0, .events = {{0, {0x90, 69, 100}}}});
    rig.engine.setSession({.tempo = kTempo, .tracks = {track}});
    rig.engine.play();
    rig.audio.process(kBeatFrames / 4);
    const float peak = loudest(rig.audio.output("interface", 1));
    CHECK(peak > 0.2F);
    CHECK(peak <= 0.25F);
}

TEST_CASE("an instrument reactivates at a new sample rate and keeps playing") {
    const std::shared_ptr<Plugin> instrument = sine();
    CHECK(instrument->activate(48000.0, 256));
    CHECK(instrument->activate(44100.0, 512));
    std::vector<float> left(512);
    std::vector<float> right(512);
    const std::array<float*, 2> mix{left.data(), right.data()};
    instrument->queueMidi(std::array<std::uint8_t, 3>{0x90, 69, 100}, 0);
    instrument->processBlock(mix, 512, 0, {});
    CHECK(loudest(left) > 0.4F);
    CHECK_FALSE(instrument->hasEditor());
}

TEST_CASE("an instrument lists its params, and a value set while it is idle reaches its state") {
    const std::shared_ptr<Plugin> instrument = sine();
    const std::vector<PluginParam> params = instrument->params();
    REQUIRE(params.size() == 1);
    CHECK(params[0].name == "Level");
    CHECK(params[0].max == 1.0);
    CHECK(params[0].defaultValue == 0.5);
    CHECK_FALSE(params[0].stepped);
    CHECK(params[0].choices.empty());
    CHECK(instrument->paramText(0, 0.25) == "25%");
    instrument->setParam(0, 0.25);
    CHECK(instrument->paramValue(0) == 0.25);
    REQUIRE(instrument->saveState() == levelState(0.25F));
    const std::shared_ptr<Plugin> reloaded = sine();
    REQUIRE(reloaded->loadState(*instrument->saveState()));
    CHECK(reloaded->paramValue(0) == 0.25);
}

TEST_CASE("a param set while the instrument processes reaches it through its next process call") {
    const std::shared_ptr<Plugin> instrument = sine();
    REQUIRE(instrument->activate(48000.0, 512));
    std::vector<float> left(512);
    std::vector<float> right(512);
    const std::array<float*, 2> mix{left.data(), right.data()};
    instrument->queueMidi(std::array<std::uint8_t, 3>{0x90, 69, 100}, 0);
    instrument->processBlock(mix, 512, 0, {});
    {
        const std::jthread audio([&](const std::stop_token& stop) {
            while (!stop.stop_requested()) {
                instrument->processBlock(mix, 512, 0, {});
            }
        });
        instrument->setParam(0, 0.25);
        CHECK(instrument->paramValue(0) == 0.25);
    }
    std::ranges::fill(left, 0.0F);
    instrument->processBlock(mix, 512, 0, {});
    CHECK(loudest(left) > 0.2F);
    CHECK(loudest(left) <= 0.25F);
}
