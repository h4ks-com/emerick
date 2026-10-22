#include <sndfile.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;
using Catch::Approx;

namespace {

float sampleAt(const std::filesystem::path& path, core::Frames frame) {
    SF_INFO info{};
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
    REQUIRE(file != nullptr);
    std::vector<float> samples(static_cast<std::size_t>(info.channels));
    sf_seek(file, frame, SEEK_SET);
    sf_readf_float(file, samples.data(), 1);
    sf_close(file);
    return samples.at(0);
}

std::string importOnes(ProjectRig& rig, const std::string& track, const std::string& at) {
    writeConstant(rig.path("ones.wav"), 48000, 1.0F);
    return rig.run("clip.import", {{"track", track}, {"path", rig.path("ones.wav").string()}, {"at", at}}).at("id");
}

}  // namespace

TEST_CASE("clip fades take shapes and curves, and a shape change on the selection undoes as one step") {
    ProjectRig rig;
    const std::string track = rig.run("track.addAudio").at("id");
    const std::string first = importOnes(rig, track, "1.1.0");
    const std::string second = importOnes(rig, track, "3.1.0");

    const Json fade = rig.run("clip.setFade", {{"clip", first}, {"fadeInShape", "sCurve"}, {"fadeOutCurve", 3}});
    CHECK(fade.at("fadeInShape") == "sCurve");
    CHECK(fade.at("fadeOutCurve") == 1.0);
    CHECK_THROWS_AS(rig.run("clip.setFade", {{"clip", first}, {"fadeOutShape", "wobbly"}}), AppError);

    rig.run("selection.set", {{"clips", {first, second}}});
    const Json shaped = rig.run("clip.setFadeOutShape", {{"shape", "exponential"}});
    CHECK(shaped.at("clips").at(0).at("fadeOutShape") == "exponential");
    CHECK(shaped.at("clips").at(1).at("fadeOutShape") == "exponential");
    rig.controller->project().undo();
    const Json clips = projectState(*rig.controller).at("tracks").at(0).at("clips");
    CHECK(clips.at(0).at("fadeOutShape") == "linear");
    CHECK(clips.at(1).at("fadeOutShape") == "linear");
    CHECK(clips.at(0).at("fadeInShape") == "sCurve");
}

TEST_CASE("create crossfade fades overlapping clips over the overlap, and export plays the shapes") {
    ProjectRig rig;
    const std::string track = rig.run("track.addAudio", {{"name", "Pad"}}).at("id");
    const std::string first = importOnes(rig, track, "1.1.0");
    const std::string second = importOnes(rig, track, "2.1.0");
    CHECK_THROWS_AS(rig.run("clip.crossfade", {{"clip", first}}), AppError);
    rig.run("clip.move", {{"clip", second}, {"start", 1440}});

    const Json faded = rig.run("clip.crossfade", {{"clip", second}});
    CHECK(faded.at("clips").at(0).at("fadeIn") == 480);
    CHECK(faded.at("clips").at(0).at("fadeInShape") == "equalPower");
    const Json clips = projectState(*rig.controller).at("tracks").at(0).at("clips");
    CHECK(clips.at(0).at("fadeOut") == 480);
    CHECK(clips.at(0).at("fadeOutShape") == "equalPower");

    rig.run("export.stems", {{"folder", rig.path("stems").string()}});
    const auto stem = rig.path("stems") / "Pad.wav";
    const auto at = [&](core::Ticks ticks) {
        return sampleAt(stem, rig.controller->project().document().tempo.ticksToFrames(ticks, kRate));
    };
    CHECK(at(720) == 1.0F);
    CHECK(at(1440 + 240) == Approx(2 * 0.7071068F).margin(1e-3));
    CHECK(at(2880) == 1.0F);
}
