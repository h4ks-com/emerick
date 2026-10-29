#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include "project_rig.hpp"

using Catch::Approx;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

std::vector<float> sine(double hz, double amplitude, double seconds) {
    std::vector<float> samples(static_cast<std::size_t>(seconds * kRate));
    for (std::size_t frame = 0; frame < samples.size(); ++frame) {
        samples[frame] =
            static_cast<float>(amplitude * std::sin(2 * std::numbers::pi * hz * static_cast<double>(frame) / kRate));
    }
    return samples;
}

}  // namespace

TEST_CASE("audio analyze measures a file on the project's bar grid") {
    ProjectRig rig;
    const std::vector<float> tone = sine(1000, 0.1, 4);
    writeWav(rig.path("tone.wav"), {tone, tone}, kRate);

    const Json whole = rig.run("audio.analyze", {{"path", rig.path("tone.wav").string()}});
    CHECK(whole.at("integratedLufs").get<double>() == Approx(-20.0).margin(0.1));
    CHECK(whole.at("samplePeakDbfs").get<double>() == Approx(-20.0).margin(0.1));
    CHECK(whole.at("bars").size() == 2);
    CHECK(whole.at("bars")[0].at("rmsDb").get<double>() == Approx(-23.0).margin(0.1));
    CHECK(whole.at("bandsDb").at("mid").get<double>() > whole.at("bandsDb").at("bass").get<double>());
    CHECK(whole.at("silence").empty());
    CHECK(whole.at("clippedSamples") == 0);
    CHECK(whole.at("stereo").at("correlation") == 1.0);

    const Json second = rig.run("audio.analyze", {{"path", rig.path("tone.wav").string()}, {"from", 2}, {"to", 4}});
    REQUIRE(second.at("bars").size() == 1);
    CHECK(second.at("bars")[0].at("bar") == 2);
    CHECK_THROWS_AS(rig.run("audio.analyze", {{"path", rig.path("tone.wav").string()}, {"from", 5}}), AppError);
}

TEST_CASE("render analyze measures the rendered mix") {
    ProjectRig rig;
    CHECK_THROWS_AS(rig.run("render.analyze"), AppError);

    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    writeWav(rig.path("tone.wav"), {sine(1000, 0.1, 2)}, kRate);
    rig.run("clip.import", {{"track", vox}, {"path", rig.path("tone.wav").string()}});

    const Json mix = rig.run("render.analyze");
    CHECK(mix.at("channels") == 2);
    CHECK(mix.at("bars").size() == 1);
    CHECK(mix.at("integratedLufs").is_number());
    CHECK(mix.at("stereo").at("correlation") == 1.0);
}
