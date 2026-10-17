#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

Json savedProject(const std::filesystem::path& folder) {
    std::ifstream file{folder / "project.json"};
    return Json::parse(file);
}

}  // namespace

TEST_CASE("transport actions return the state the engine reached") {
    ProjectRig rig;
    CHECK(rig.run("transport.play").at("playing") == true);
    const Json stopped = rig.run("transport.stop");
    CHECK(stopped.at("playing") == false);
    CHECK(stopped.at("position").at("ticks").get<core::Ticks>() > 0);
    CHECK(rig.run("transport.locate", {{"position", "3.1.0"}}).at("position").at("bbt") == "3.1.0");
    const Json recording = rig.run("transport.record");
    CHECK(recording.at("playing") == true);
    CHECK(recording.at("recording") == true);
    CHECK(rig.run("transport.stop").at("recording") == false);
}

TEST_CASE("saving while recording stops the transport and keeps every take") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    rig.run("track.setArm", {{"track", vox}, {"value", true}});
    rig.run("track.setArm", {{"track", keys}, {"value", true}});
    rig.run("transport.record");
    rig.audio->setInput("interface", 1, std::vector<float>(4800, 0.5F));
    rig.midi->inject(kMidiPort, std::vector<std::uint8_t>{0x90, 60, 100},
                     rig.audio->hostTimeAt(rig.audio->processedFrames()));
    rig.audio->process(4800);

    const auto folder = rig.path("song");
    const Json saved = rig.run("project.saveAs", {{"path", folder.string()}});
    CHECK(rig.controller->recording() == false);
    CHECK(saved.at("tracks").at(0).at("clips").size() == 1);
    CHECK(saved.at("tracks").at(1).at("clips").size() == 1);

    const Json file = savedProject(folder);
    const Json& audioClip = file.at("tracks").at(0).at("clips").at(0);
    CHECK(std::filesystem::exists(folder / audioClip.at("file").get<std::string>()));
    CHECK(file.at("tracks").at(1).at("clips").size() == 1);
}

TEST_CASE("a take the engine could not write is reported in the project state") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    std::filesystem::remove_all(rig.controller->folder() / "audio");
    std::ofstream{rig.controller->folder() / "audio"} << "not a folder";
    rig.run("track.setArm", {{"track", vox}, {"value", true}});
    rig.run("transport.record");
    rig.audio->process(4800);
    rig.run("transport.stop");
    REQUIRE(rig.waitForTakes(1) == 1);
    const Json state = projectState(*rig.controller);
    REQUIRE(state.contains("takeErrors"));
    CHECK(state.at("takeErrors").at(0).at("track") == vox);
    CHECK(state.at("tracks").at(0).at("clips").empty());
}

TEST_CASE("a clip the engine skips shows in the project state and stops an export") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    writeConstant(rig.path("ones.wav"), 4800, 1.0F);
    const Json clip = rig.run("clip.import", {{"track", vox}, {"path", rig.path("ones.wav").string()}});
    std::filesystem::remove(rig.controller->folder() / clip.at("file").get<std::string>());
    rig.run("clip.move", {{"clip", clip.at("id")}, {"start", 960}});
    CHECK(projectState(*rig.controller).at("skippedClips").at(0).at("track") == vox);
    CHECK_THROWS_AS(rig.run("export.mix", {{"path", rig.path("mix.wav").string()}}), AppError);
}

TEST_CASE("saving right after recording writes the take into project.json") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    rig.run("track.setArm", {{"track", vox}, {"value", true}});
    const auto folder = rig.path("song");
    rig.run("project.saveAs", {{"path", folder.string()}});
    rig.run("transport.record");
    rig.audio->process(4800);
    rig.run("project.save");
    CHECK(savedProject(folder).at("tracks").at(0).at("clips").size() == 1);
}
