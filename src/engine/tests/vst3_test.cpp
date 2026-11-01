#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
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

TEST_CASE("a VST3 editor opens in a parent at the size it asks for, takes the sizes its window gets and closes") {
    const std::shared_ptr<Plugin> effect = gain();
    REQUIRE(effect->hasEditor());
    CHECK_FALSE(effect->embedEditor(nullptr).has_value());
    CHECK_FALSE(effect->editorOpen());
    int parent = 0;
    const auto opened = effect->embedEditor(&parent);
    REQUIRE(opened.has_value());
    CHECK(*opened == EditorSize{.width = 400, .height = 250});
    CHECK(effect->editorOpen());
    CHECK_FALSE(effect->takeEditorResize().has_value());
    CHECK(effect->editorResizable());
    CHECK(effect->resizeEditor({.width = 50, .height = 600}) == EditorSize{.width = 100, .height = 600});
    effect->closeEditor();
    CHECK_FALSE(effect->editorOpen());
    CHECK_FALSE(effect->editorResizable());
}

TEST_CASE("an edit in the VST3 editor reaches the processor and marks the state changed once per gesture") {
    const std::shared_ptr<Plugin> effect = gain();
    REQUIRE(effect->activate(48000.0, kFrames));
    CHECK_FALSE(effect->idle());
    int parent = 0;
    REQUIRE(effect->embedEditor(&parent).has_value());
    CHECK(effect->takeParamsChanged());
    CHECK(effect->idle());
    CHECK_FALSE(effect->idle());
    CHECK(effect->paramValue(kGainParam) == 0.25);
    Stereo quieter;
    effect->processEffect(quieter.channels, kFrames, 0, {});
    CHECK(quieter.left.back() == 0.125F);
    const std::shared_ptr<Plugin> restored = gain();
    REQUIRE(restored->loadState(*effect->saveState()));
    CHECK(restored->paramValue(kGainParam) == 0.25);
    CHECK_FALSE(restored->idle());
}

namespace {

constexpr const char* kSynthId = "454D525453594E310000000000000001";

std::shared_ptr<Plugin> synth() {
    auto created = createVst3Plugin(EMERICK_TEST_SYNTH_VST3, kSynthId);
    REQUIRE(created.has_value());
    return *created;
}

struct Block {
    std::vector<float> left = std::vector<float>(kFrames);
    std::vector<float> right = std::vector<float>(kFrames);
    std::array<float*, 2> mix{left.data(), right.data()};
};

float peakOf(const std::vector<float>& samples) {
    return std::ranges::max(samples | std::views::transform([](float sample) { return std::abs(sample); }));
}

int risingZeroCrossings(const std::vector<float>& samples) {
    return static_cast<int>(std::ranges::count_if(std::views::iota(std::size_t{1}, samples.size()), [&](std::size_t i) {
        return samples[i - 1] < 0.0F && samples[i] >= 0.0F;
    }));
}

}  // namespace

TEST_CASE("a VST3 instrument plays notes with their velocity, stops at note off and sends the notes back out") {
    const auto described = describeBundle(EMERICK_TEST_SYNTH_VST3);
    REQUIRE(described.has_value());
    CHECK(isInstrument(described->front()));
    const std::shared_ptr<Plugin> instrument = synth();
    CHECK(instrument->producesMidi());
    REQUIRE(instrument->activate(48000.0, kFrames));
    instrument->queueMidi(std::array<std::uint8_t, 3>{0x90, 69, 127}, 0);
    Block loud;
    instrument->processBlock(loud.mix, kFrames, 0, {});
    CHECK(peakOf(loud.left) == Catch::Approx(0.5F).margin(0.01));
    REQUIRE(instrument->producedMidi().size() == 1);
    CHECK(std::ranges::equal(instrument->producedMidi().front().bytes, std::array<std::uint8_t, 3>{0x90, 69, 127}));
    instrument->queueMidi(std::array<std::uint8_t, 3>{0x80, 69, 0}, 0);
    Block silent;
    instrument->processBlock(silent.mix, kFrames, 0, {});
    CHECK(peakOf(silent.left) == 0.0F);
}

TEST_CASE("a VST3 instrument takes controllers, pitch bend and program change through its MIDI mapping") {
    const std::shared_ptr<Plugin> instrument = synth();
    REQUIRE(instrument->activate(48000.0, 4800));
    instrument->queueMidi(std::array<std::uint8_t, 3>{0x90, 69, 127}, 0);
    instrument->queueMidi(std::array<std::uint8_t, 3>{0xB0, 7, 127}, 0);
    std::vector<float> left(4800);
    std::vector<float> right(4800);
    const std::array<float*, 2> mix{left.data(), right.data()};
    instrument->processBlock(mix, 4800, 0, {});
    CHECK(instrument->paramValue(1) == 1.0);
    CHECK(risingZeroCrossings(left) == Catch::Approx(44).margin(1));

    instrument->queueMidi(std::array<std::uint8_t, 3>{0xE0, 0x7F, 0x7F}, 0);
    instrument->queueMidi(std::array<std::uint8_t, 2>{0xC0, 2}, 0);
    std::ranges::fill(left, 0.0F);
    instrument->processBlock(mix, 4800, 0, {});
    CHECK(risingZeroCrossings(left) == Catch::Approx(49).margin(1));
    CHECK(instrument->paramText(3, *instrument->paramValue(3)) == "Bright");

    const std::shared_ptr<Plugin> restored = synth();
    REQUIRE(restored->loadState(*instrument->saveState()));
    CHECK(restored->paramValue(1) == 1.0);
    CHECK(restored->paramValue(2) == 1.0);
    CHECK(restored->paramValue(3) == 2.0);
}
