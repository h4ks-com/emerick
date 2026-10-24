#include <sndfile.h>

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "emerick/app/session_mapping.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

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

engine::Session session(const ProjectRig& rig) {
    return toSession(rig.controller->project().document(), rig.registry, rig.controller->folder());
}

}  // namespace

TEST_CASE("a muted clip plays and exports nothing, the clips beneath it play, and undo unmutes it") {
    ProjectRig rig;
    const std::string track = rig.run("track.addAudio", {{"name", "Pad"}}).at("id");
    importOnes(rig, track, "1.1.0");
    const std::string over = importOnes(rig, track, "3.1.0");
    rig.run("clip.move", {{"clip", over}, {"start", 0}});
    rig.run("clip.setGain", {{"clip", over}, {"db", -6.0206}});

    const Json muted = rig.run("clip.setMute", {{"clip", over}});
    CHECK(muted.at("clips").at(0).at("muted") == true);
    CHECK(rig.controller->project().undoName() == "mute clip");
    REQUIRE(session(rig).tracks.at(0).audioClips.size() == 1);
    CHECK(session(rig).tracks.at(0).audioClips.at(0).gain == 1.0F);

    rig.run("export.stems", {{"folder", rig.path("stems").string()}});
    CHECK(sampleAt(rig.path("stems") / "Pad.wav", 1000) == 1.0F);

    rig.run("project.undo");
    CHECK(session(rig).tracks.at(0).audioClips.size() == 2);
    CHECK(projectState(*rig.controller).at("tracks").at(0).at("clips").at(1).at("muted") == false);
}

TEST_CASE("clip.setMute mutes the selection unless every selected clip is muted, as one undo step") {
    ProjectRig rig;
    const std::string track = rig.run("track.addAudio", {{"name", "Pad"}}).at("id");
    const std::string first = importOnes(rig, track, "1.1.0");
    const std::string second = importOnes(rig, track, "3.1.0");
    rig.run("clip.setMute", {{"clip", first}});

    rig.run("selection.set", {{"clips", {first, second}}});
    Json result = rig.run("clip.setMute");
    CHECK(result.at("clips").at(0).at("muted") == true);
    CHECK(result.at("clips").at(1).at("muted") == true);
    result = rig.run("clip.setMute");
    CHECK(result.at("clips").at(0).at("muted") == false);
    CHECK(result.at("clips").at(1).at("muted") == false);
    rig.run("project.undo");
    const Json clips = projectState(*rig.controller).at("tracks").at(0).at("clips");
    CHECK(clips.at(0).at("muted") == true);
    CHECK(clips.at(1).at("muted") == true);
    CHECK(rig.run("clip.setMute", {{"clips", {second}}, {"value", false}}).at("clips").at(0).at("muted") == false);
    CHECK_THROWS_AS(rig.run("clip.setMute", {{"clips", {"missing"}}}), AppError);
}
