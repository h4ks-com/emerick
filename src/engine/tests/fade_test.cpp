#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "emerick/engine/engine.hpp"
#include "rig.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;
using Catch::Approx;
using emerick::core::FadeShape;

namespace {

// At 120 BPM a beat of 960 ticks lasts 24000 frames, so a fade of 240 ticks lasts 6000.
const emerick::core::TempoMap kTempo{120.0, {4, 4}};
constexpr Ticks kFadeTicks = 240;
constexpr Frames kFadeFrames = 6000;

struct ShapeCase {
    FadeShape shape;
    std::array<float, 3> gains;  // at 25, 50 and 75 percent of a fade in
};

const std::array kShapeCases{
    ShapeCase{FadeShape::linear, {0.25F, 0.5F, 0.75F}},
    ShapeCase{FadeShape::equalPower, {0.3826834F, 0.7071068F, 0.9238795F}},
    ShapeCase{FadeShape::exponential, {0.015625F, 0.125F, 0.421875F}},
    ShapeCase{FadeShape::logarithmic, {0.578125F, 0.875F, 0.984375F}},
    ShapeCase{FadeShape::sCurve, {0.1464466F, 0.5F, 0.8535534F}},
};

AudioClip constantClip(const std::filesystem::path& path, Ticks start, Ticks length) {
    return {.path = path, .start = start, .length = length, .sourceOffset = 0, .gain = 1.0F};
}

// Plays the track from bar 1 and returns its first output channel from the first played frame on.
std::vector<float> render(const Track& track, Frames frames) {
    Rig rig;
    rig.engine.setSession({.tempo = kTempo, .tracks = {track}});
    playOnceReadAhead(rig);
    const Frames start = rig.audio.processedFrames();
    rig.audio.process(frames);
    const auto output = rig.audio.output("keyboard", 1);
    return {output.begin() + start, output.end()};
}

Track padTrack() {
    Track track{.id = "pad", .kind = TrackKind::audio};
    track.audioOut = {"keyboard", {1, 2}};
    return track;
}

}  // namespace

TEST_CASE("each fade shape plays its gain to the sample at a quarter, half and three quarters of the fade") {
    const TempFolder folder;
    const auto ones = folder.path / "ones.wav";
    writeWav(ones, std::vector<float>(48000, 1.0F));
    for (const ShapeCase& shapeCase : kShapeCases) {
        INFO(static_cast<int>(shapeCase.shape));
        Track track = padTrack();
        AudioClip clip = constantClip(ones, 0, 960);
        clip.fadeIn = kFadeTicks;
        clip.fadeOut = kFadeTicks;
        clip.fadeInCurve = emerick::core::fadeCurve(shapeCase.shape, 0.0);
        clip.fadeOutCurve = clip.fadeInCurve;
        track.audioClips.push_back(clip);
        const std::vector<float> output = render(track, 24000);
        for (std::size_t quarter = 0; quarter < 3; ++quarter) {
            const auto at = static_cast<Frames>(quarter + 1) * kFadeFrames / 4;
            CHECK(output.at(static_cast<std::size_t>(at)) == Approx(shapeCase.gains.at(quarter)).margin(1e-5));
            CHECK(output.at(static_cast<std::size_t>(24000 - at)) == Approx(shapeCase.gains.at(quarter)).margin(1e-5));
        }
    }
}

TEST_CASE("a curve bends a fade through its midpoint") {
    CHECK(fadeGain(50, 1000, 100, 0, emerick::core::fadeCurve(FadeShape::linear, 0.5)) == Approx(0.8F));
    CHECK(fadeGain(50, 1000, 100, 0, emerick::core::fadeCurve(FadeShape::linear, -0.5)) == Approx(0.2F));
    const double curve = emerick::core::curveThroughMidpoint(FadeShape::equalPower, 0.5);
    CHECK(fadeGain(50, 1000, 100, 0, emerick::core::fadeCurve(FadeShape::equalPower, curve)) == Approx(0.5F));
}

TEST_CASE("a later clip plays over an earlier one where they overlap") {
    const TempFolder folder;
    const auto ones = folder.path / "ones.wav";
    const auto half = folder.path / "half.wav";
    writeWav(ones, std::vector<float>(48000, 1.0F));
    writeWav(half, std::vector<float>(48000, 0.5F));
    Track track = padTrack();
    track.audioClips = {constantClip(ones, 0, 1920), constantClip(half, 480, 480)};

    const std::vector<float> output = render(track, 48000);

    CHECK(output.at(6000) == 1.0F);
    CHECK(output.at(18000) == 0.5F);
    CHECK(output.at(30000) == 1.0F);
}

TEST_CASE("an equal power crossfade keeps the power constant over the overlap") {
    const TempFolder folder;
    const auto ones = folder.path / "ones.wav";
    const auto minusOnes = folder.path / "minus.wav";
    writeWav(ones, std::vector<float>(48000, 1.0F));
    writeWav(minusOnes, std::vector<float>(48000, -1.0F));
    const auto crossfaded = [&](const std::filesystem::path& second) {
        Track track = padTrack();
        AudioClip first = constantClip(ones, 0, 960);
        first.fadeOut = kFadeTicks;
        first.fadeOutCurve = emerick::core::fadeCurve(FadeShape::equalPower, 0.0);
        AudioClip later = constantClip(second, 960 - kFadeTicks, 960);
        later.fadeIn = kFadeTicks;
        later.fadeInCurve = first.fadeOutCurve;
        track.audioClips = {first, later};
        return render(track, 48000);
    };

    const std::vector<float> sum = crossfaded(ones);
    const std::vector<float> difference = crossfaded(minusOnes);

    for (Frames at = 18000; at < 24000; at += 250) {
        const auto index = static_cast<std::size_t>(at);
        const float out = (sum.at(index) + difference.at(index)) / 2.0F;
        const float in = (sum.at(index) - difference.at(index)) / 2.0F;
        CHECK(out * out + in * in == Approx(1.0F).margin(1e-3));
    }
    CHECK(sum.at(12000) == 1.0F);
    CHECK(sum.at(30000) == 1.0F);
}
