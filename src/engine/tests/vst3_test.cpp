#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
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

constexpr const char* kGainId = "454D525447414E310000000000000001";
constexpr std::uint32_t kGainParam = 1;
constexpr std::uint32_t kPolarityParam = 2;
constexpr Frames kFrames = 256;

std::shared_ptr<Plugin> gain() {
    auto created = createVst3Plugin(EMERICK_TEST_GAIN_VST3, kGainId);
    REQUIRE(created.has_value());
    return *created;
}

struct Stereo {
    std::vector<float> left = std::vector<float>(kFrames, 0.5F);
    std::vector<float> right = std::vector<float>(kFrames, -0.25F);
    std::array<float*, 2> channels{left.data(), right.data()};
};

Track trackWith(std::vector<Insert> inserts, const std::filesystem::path& clip) {
    Track track{.id = "guitar", .kind = TrackKind::audio};
    track.audioOut = {"interface", {1, 2}};
    track.audioClips = {{.path = clip, .start = 0, .length = 1920, .sourceOffset = 0, .gain = 1.0F}};
    track.inserts = std::move(inserts);
    return track;
}

}  // namespace

TEST_CASE("a VST3 bundle lists its effects with their subcategories") {
    const auto described = describeBundle(EMERICK_TEST_GAIN_VST3);
    REQUIRE(described.has_value());
    REQUIRE(described->size() == 1);
    const PluginDescriptor& plugin = described->front();
    CHECK(plugin.format == PluginFormat::vst3);
    CHECK(plugin.id == kGainId);
    CHECK(plugin.name == "Test Gain");
    CHECK(plugin.vendor == "Emerick Tests");
    CHECK(plugin.features == std::vector<std::string>{"Fx", "Dynamics"});
    CHECK(isEffect(plugin));
    CHECK_FALSE(isInstrument(plugin));
}

TEST_CASE("a VST3 bundle with a module info is described from it without loading its binary") {
    // The binary of this bundle aborts when it loads, so this test only passes when the scan never loads it.
    const auto described = describeBundle(EMERICK_TEST_DESCRIBED_VST3);
    REQUIRE(described.has_value());
    REQUIRE(described->size() == 1);
    CHECK(described->front().id == "454D525447414E310000000000000002");
    CHECK(described->front().features == std::vector<std::string>{"Fx", "Dynamics"});
}

TEST_CASE("a VST3 effect scales its input by its gain param and lists its params with their choices") {
    const std::shared_ptr<Plugin> effect = gain();
    const std::vector<PluginParam> params = effect->params();
    REQUIRE(params.size() == 2);
    CHECK(params[0].name == "Gain");
    CHECK(params[0].max == 2.0);
    CHECK(params[0].defaultValue == 1.0);
    CHECK(params[1].name == "Polarity");
    CHECK(params[1].stepped);
    CHECK(params[1].choices == std::vector<std::string>{"Normal", "Inverted"});
    CHECK(effect->paramText(kGainParam, 0.5) == "0.50x");
    REQUIRE(effect->activate(48000.0, kFrames));
    Stereo unchanged;
    effect->processEffect(unchanged.channels, kFrames, 0, {});
    CHECK(unchanged.left.back() == 0.5F);
    CHECK(unchanged.right.back() == -0.25F);
    effect->setParam(kGainParam, 0.5);
    CHECK(effect->paramValue(kGainParam) == 0.5);
    Stereo halved;
    effect->processEffect(halved.channels, kFrames, 0, {});
    CHECK(halved.left.back() == 0.25F);
    CHECK(halved.right.front() == -0.125F);
}

TEST_CASE("a param set while the VST3 effect processes reaches it through its next process call") {
    const std::shared_ptr<Plugin> effect = gain();
    REQUIRE(effect->activate(48000.0, kFrames));
    Stereo running;
    {
        const std::jthread audio([&](const std::stop_token& stop) {
            while (!stop.stop_requested()) {
                effect->processEffect(running.channels, kFrames, 0, {});
                std::ranges::fill(running.left, 0.5F);
            }
        });
        effect->setParam(kPolarityParam, 1.0);
    }
    Stereo inverted;
    effect->processEffect(inverted.channels, kFrames, 0, {});
    CHECK(inverted.left.front() == -0.5F);
}

TEST_CASE("a VST3 effect saves its state and a fresh instance plays as the state says") {
    const std::shared_ptr<Plugin> effect = gain();
    effect->setParam(kGainParam, 1.5);
    const auto state = effect->saveState();
    REQUIRE(state.has_value());
    const std::shared_ptr<Plugin> restored = gain();
    REQUIRE(restored->loadState(*state));
    CHECK(restored->paramValue(kGainParam) == 1.5);
    REQUIRE(restored->activate(48000.0, kFrames));
    Stereo louder;
    restored->processEffect(louder.channels, kFrames, 0, {});
    CHECK(louder.left.back() == 0.75F);
    CHECK_FALSE(restored->loadState(std::vector<std::uint8_t>{1, 2, 3}));
}

TEST_CASE("a plugin insert plays in the track's chain, and a bypassed one passes the audio through") {
    TempFolder folder;
    const std::filesystem::path clip = folder.path / "dc.wav";
    writeWav(clip, std::vector<float>(4800, 0.5F));
    const std::shared_ptr<Plugin> effect = gain();
    effect->setParam(kGainParam, 0.5);
    const auto playOnce = [&](bool bypass) {
        Rig rig;
        rig.engine.setSession({.tracks = {trackWith({{.id = "amp", .bypass = bypass, .plugin = effect}}, clip)}});
        playOnceReadAhead(rig);
        const Frames start = rig.audio.processedFrames();
        rig.audio.process(1024);
        return rig.audio.output("interface", 1).at(static_cast<std::size_t>(start + 512));
    };
    CHECK(std::abs(playOnce(false)) == 0.25F);
    CHECK(std::abs(playOnce(true)) == 0.5F);
}

TEST_CASE("an automation lane moves a plugin insert's param") {
    TempFolder folder;
    const std::filesystem::path clip = folder.path / "dc.wav";
    writeWav(clip, std::vector<float>(48000, 0.5F));
    const std::shared_ptr<Plugin> effect = gain();
    Track track = trackWith({{.id = "amp", .plugin = effect}}, clip);
    const auto quarter = static_cast<float>(effect->processValue(kGainParam, 0.5));
    track.automation = {{.target = AutomationTarget::insert,
                         .bus = {},
                         .points = {{.tick = 0, .value = quarter}},
                         .insert = "amp",
                         .param = kGainParam}};
    Rig rig;
    rig.engine.setSession({.tracks = {track}});
    playOnceReadAhead(rig);
    const Frames start = rig.audio.processedFrames();
    rig.audio.process(2048);
    CHECK(std::abs(rig.audio.output("interface", 1).at(static_cast<std::size_t>(start + 1024))) == 0.25F);
}
