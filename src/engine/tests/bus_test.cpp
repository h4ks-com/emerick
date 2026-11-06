#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <random>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/engine/effects.hpp"
#include "emerick/engine/engine.hpp"
#include "rig.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;
using Catch::Approx;

namespace {

constexpr Frames kSettled = 12000;

struct Output {
    std::vector<float> left;
    std::vector<float> right;
};

struct BusRig {
    TempFolder folder;
    std::filesystem::path ones = folder.path / "ones.wav";
    std::filesystem::path half = folder.path / "half.wav";
    Rig rig;

    BusRig() {
        writeWav(ones, std::vector<float>(48000, 1.0F));
        writeWav(half, std::vector<float>(48000, 0.5F));
    }

    void start(Session session) {
        rig.engine.setSession(std::move(session));
        playOnceReadAhead(rig);
    }

    Output play(Frames frames) {
        const Frames start = rig.audio.processedFrames();
        rig.audio.process(frames);
        const auto from = [&](int channel) {
            const auto output = rig.audio.output("keyboard", channel);
            return std::vector<float>(output.begin() + start, output.end());
        };
        return {from(1), from(2)};
    }
};

Track source(const std::string& id, const std::filesystem::path& file) {
    Track track{.id = id, .kind = TrackKind::audio};
    track.audioClips = {{.path = file, .start = 0, .length = 1920, .sourceOffset = 0, .gain = 1.0F}};
    return track;
}

Track bus(const std::string& id) {
    Track track{.id = id, .kind = TrackKind::bus};
    track.audioOut = {"keyboard", {1, 2}};
    return track;
}

}  // namespace

TEST_CASE("a post-fader send follows the track's fader and a pre-fader send ignores it") {
    for (const bool pre : {false, true}) {
        INFO(pre);
        BusRig rig;
        Track vox = source("vox", rig.ones);
        vox.gain = 0.5F;
        vox.sends = {{.bus = "verb", .gain = 0.5F, .pre = pre}};
        rig.start({.tracks = {vox, bus("verb")}});
        const Output output = rig.play(24000);
        CHECK(output.left.at(kSettled) == Approx(pre ? 0.5F : 0.25F));
        CHECK(output.right.at(kSettled) == Approx(pre ? 0.5F : 0.25F));
    }
}

TEST_CASE("a bus applies its own gain and pan to what its sources send") {
    BusRig rig;
    Track vox = source("vox", rig.ones);
    vox.sends = {{.bus = "verb", .gain = 1.0F, .pre = false}};
    Track verb = bus("verb");
    verb.gain = 0.5F;
    verb.pan = 1.0F;
    rig.start({.tracks = {verb, vox}, .panLaw = PanLaw::zeroDb});
    const Output output = rig.play(24000);
    CHECK(output.left.at(kSettled) == Approx(0.0F).margin(1e-6));
    CHECK(output.right.at(kSettled) == Approx(0.5F));
}

TEST_CASE("a track with an output bus plays only through that bus") {
    BusRig rig;
    Track vox = source("vox", rig.ones);
    vox.audioOut = {"keyboard", {1, 2}};
    vox.outputBus = "group";
    Track group = bus("group");
    group.gain = 0.5F;
    rig.start({.tracks = {vox, group}});
    CHECK(rig.play(24000).left.at(kSettled) == Approx(0.5F));
}

TEST_CASE("soloing a track keeps the buses it sends to audible, and soloing a bus keeps the tracks feeding it") {
    BusRig rig;
    Track vox = source("vox", rig.ones);
    vox.sends = {{.bus = "verb", .gain = 1.0F, .pre = false}};
    Track drums = source("drums", rig.half);
    drums.audioOut = {"keyboard", {1, 2}};
    rig.start({.tracks = {vox, drums, bus("verb")}});
    // A track that falls silent stops reading its clips, so we let the reader catch up after each solo.
    const auto level = [&] {
        rig.play(kBlock);
        REQUIRE(waitFor([&] { return rig.rig.engine.readAheadReady(); }));
        return rig.play(kBlock * 4).left.back();
    };
    CHECK(level() == Approx(1.5F));

    rig.rig.engine.setSolo("vox", true);
    CHECK(level() == Approx(1.0F));

    rig.rig.engine.setSolo("vox", false);
    rig.rig.engine.setSolo("verb", true);
    CHECK(level() == Approx(1.0F));

    rig.rig.engine.setSolo("drums", true);
    CHECK(level() == Approx(1.5F));
}

TEST_CASE("a new send level ramps from the old one across a block, so a level change never clicks") {
    BusRig rig;
    Track vox = source("vox", rig.ones);
    vox.sends = {{.bus = "verb", .gain = 1.0F, .pre = false}};
    rig.start({.tracks = {vox, bus("verb")}});
    rig.play(kBlock * 4);

    vox.sends.front().gain = 0.0F;
    rig.rig.engine.setSession({.tracks = {vox, bus("verb")}});
    const Output output = rig.play(kBlock * 2);
    CHECK(output.left.front() > 0.9F);
    CHECK(output.left.at(kBlock / 2) == Approx(0.5F).margin(0.02));
    CHECK(output.left.back() == Approx(0.0F).margin(1e-6));
}

TEST_CASE("buses that feed each other in a loop stay silent and report the loop") {
    BusRig rig;
    Track first = bus("first");
    first.sends = {{.bus = "second", .gain = 1.0F, .pre = false}};
    Track second = bus("second");
    second.outputBus = "first";
    rig.rig.engine.setSession({.tracks = {first, second}});
    CHECK(rig.rig.engine.sessionErrors().size() == 2);
}

TEST_CASE("mixing the buses a block behind the tracks plays exactly the same samples a block later") {
    TempFolder folder;
    std::mt19937 random(3);
    std::uniform_real_distribution<float> spread(-0.5F, 0.5F);
    std::vector<float> noise(96000);
    std::ranges::generate(noise, [&] { return spread(random); });
    writeWav(folder.path / "noise.wav", noise);

    const auto clip = [&](const std::string& id) {
        Track track{.id = id, .kind = TrackKind::audio};
        track.audioClips = {{.path = folder.path / "noise.wav", .start = 0, .length = 3840, .sourceOffset = 0}};
        return track;
    };
    Track vox = clip("vox");
    vox.outputBus = "group";
    vox.captureOut = {"interface", {1, 2}};
    vox.sends = {{.bus = "verb", .gain = 0.7F, .pre = true}, {.bus = "echo", .gain = 0.4F, .pre = false}};
    vox.automation = {
        {.target = AutomationTarget::gain, .points = {{.tick = 0, .value = 1.0F}, {.tick = 1920, .value = 0.2F}}}};
    Track bass = clip("bass");
    bass.outputBus = "group";
    bass.pan = -0.5F;
    Track verb = bus("verb");
    verb.outputBus = "group";
    verb.inserts = {
        {.id = "verb-reverb", .type = EffectType::reverb, .params = defaultParams(effectSpec(EffectType::reverb))}};
    Track echo = bus("echo");
    echo.outputBus = "group";
    echo.sends = {{.bus = "verb", .gain = 0.3F, .pre = false}};
    echo.inserts = {
        {.id = "echo-delay", .type = EffectType::delay, .params = defaultParams(effectSpec(EffectType::delay))}};
    Track group = bus("group");
    group.automation = {
        {.target = AutomationTarget::pan, .points = {{.tick = 0, .value = -0.3F}, {.tick = 3840, .value = 0.6F}}}};
    const Session session{.tracks = {vox, bass, verb, echo, group}};

    constexpr Frames kFrames = 72000;
    const auto render = [&](bool pipelined) {
        Rig rig(kBlock, 0, 0, pipelined);
        rig.engine.setSession(session);
        playOnceReadAhead(rig);
        rig.audio.process(kFrames);
        return std::pair{rig.engine.outputDelay(),
                         std::vector{rig.audio.output("keyboard", 1), rig.audio.output("keyboard", 2),
                                     rig.audio.output("interface", 1), rig.audio.output("interface", 2)}};
    };
    const auto [noDelay, serial] = render(false);
    const auto [delay, pipelined] = render(true);
    CHECK(noDelay == 0);
    REQUIRE(delay == kBlock);
    for (std::size_t channel = 0; channel < serial.size(); ++channel) {
        INFO(channel);
        const auto late = static_cast<std::ptrdiff_t>(delay);
        CHECK(std::ranges::max(serial[channel]) > 0.01F);
        CHECK(std::ranges::equal(serial[channel] | std::views::take(std::ssize(serial[channel]) - late),
                                 pipelined[channel] | std::views::drop(late)));
    }
}
