#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <vector>

#include "emerick/engine/engine.hpp"
#include "rig.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;
using Catch::Approx;
using emerick::core::AutomationCurve;

namespace {

// At the default 120 BPM a tick lasts 25 frames, so tick 960 plays at frame 24000.
constexpr Frames kFramesPerTick = 25;

struct Played {
    std::vector<float> left;
    std::vector<float> right;
};

Played play(const std::vector<AutomationLane>& lanes, PanLaw law = PanLaw::compensated) {
    TempFolder folder;
    const std::filesystem::path ones = folder.path / "ones.wav";
    writeWav(ones, std::vector<float>(48000, 1.0F));
    Rig rig;
    Track track{.id = "pad", .kind = TrackKind::audio};
    track.audioOut = {"keyboard", {1, 2}};
    track.audioClips = {{.path = ones, .start = 0, .length = 1920, .sourceOffset = 0, .gain = 1.0F}};
    track.automation = lanes;
    rig.engine.setSession({.tracks = {track}, .panLaw = law});
    playOnceReadAhead(rig);
    const Frames start = rig.audio.processedFrames();
    rig.audio.process(36000);
    const auto from = [&](int channel) {
        const auto output = rig.audio.output("keyboard", channel);
        return std::vector<float>(output.begin() + start, output.end());
    };
    return {from(1), from(2)};
}

double dbOf(float sample) { return 20.0 * std::log10(static_cast<double>(sample)); }

}  // namespace

TEST_CASE("gain automation ramps linearly in dB between two points and holds the last value after them") {
    const Played played = play({{.target = AutomationTarget::gain,
                                 .bus = {},
                                 .points = {{.tick = 0, .value = -24.0F}, {.tick = 960, .value = 0.0F}}}});
    for (const Frames frame : {Frames{6000}, Frames{12000}, Frames{18000}}) {
        INFO(frame);
        CHECK(dbOf(played.left.at(static_cast<std::size_t>(frame))) ==
              Approx(-24.0 + (24.0 * static_cast<double>(frame) / 24000.0)).margin(0.05));
    }
    CHECK(played.left.at(30000) == Approx(1.0F));
}

TEST_CASE("a hold point keeps its value until the next point, which takes over within one automation step") {
    const Played played = play(
        {{.target = AutomationTarget::gain,
          .bus = {},
          .points = {{.tick = 0, .value = 0.0F, .curve = AutomationCurve::hold}, {.tick = 480, .value = -12.0F}}}});
    const Frames step = 480 * kFramesPerTick;
    CHECK(played.left.at(static_cast<std::size_t>(step - 64)) == Approx(1.0F));
    CHECK(dbOf(played.left.at(static_cast<std::size_t>(step + 64))) == Approx(-12.0).margin(0.01));
}

TEST_CASE("pan automation moves the track from left to right") {
    const Played played = play({{.target = AutomationTarget::pan,
                                 .bus = {},
                                 .points = {{.tick = 0, .value = -1.0F}, {.tick = 960, .value = 1.0F}}}},
                               PanLaw::zeroDb);
    CHECK(played.left.at(6000) == Approx(1.0F));
    CHECK(played.right.at(6000) == Approx(0.5F).margin(0.01));
    CHECK(played.left.at(18000) == Approx(0.5F).margin(0.01));
    CHECK(played.right.at(18000) == Approx(1.0F));
}

TEST_CASE("mute automation silences the track from its point on without a click") {
    const Played played =
        play({{.target = AutomationTarget::mute,
               .bus = {},
               .points = {{.tick = 0, .value = 0.0F, .curve = AutomationCurve::hold}, {.tick = 480, .value = 1.0F}}}});
    CHECK(played.left.at(11000) == Approx(1.0F));
    CHECK(played.left.at(13000) == 0.0F);
    for (std::size_t frame = 1; frame < played.left.size(); ++frame) {
        REQUIRE(std::abs(played.left[frame] - played.left[frame - 1]) < 0.05F);
    }
}
