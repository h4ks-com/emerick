#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

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
