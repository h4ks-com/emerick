#include <catch2/catch_test_macros.hpp>
#include <string>

#include "emerick/model/json.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

const model::Track& trackOf(const ProjectRig& rig, const std::string& id) {
    return *rig.controller->project().track(model::TrackId{id});
}

}  // namespace

TEST_CASE("a send to a bus adds as one undo step, and a send or output bus that would loop is refused") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    const std::string verb = rig.run("track.addBus", {{"name", "Verb"}}).at("id");
    const std::string master = rig.run("track.addBus", {{"name", "Master"}}).at("id");
    CHECK(trackOf(rig, verb).kind == "bus");
    CHECK(trackOf(rig, verb).audioOut.deviceId == "interface");

    const Json result = rig.run("send.add", {{"track", vox}, {"bus", "Verb"}, {"db", -6.0}});
    CHECK(result.at("sends") == Json::array({{{"bus", verb}, {"db", -6.0}, {"pre", false}, {"enabled", true}}}));
    rig.run("project.undo");
    CHECK(trackOf(rig, vox).sends.empty());
    rig.run("project.redo");
    CHECK(trackOf(rig, vox).sends.size() == 1);

    rig.run("track.setOutputBus", {{"track", verb}, {"bus", master}});
    const model::Document before = rig.controller->project().document();
    CHECK_THROWS_AS(rig.run("send.add", {{"track", master}, {"bus", verb}}), AppError);
    CHECK_THROWS_AS(rig.run("track.setOutputBus", {{"track", master}, {"bus", verb}}), AppError);
    CHECK_THROWS_AS(rig.run("send.add", {{"track", verb}, {"bus", verb}}), AppError);
    CHECK_THROWS_AS(rig.run("send.add", {{"track", vox}, {"bus", verb}}), AppError);
    CHECK_THROWS_AS(rig.run("send.add", {{"track", master}, {"bus", vox}}), AppError);
    CHECK(rig.controller->project().document() == before);

    rig.run("track.setOutputBus", {{"track", verb}, {"bus", ""}});
    CHECK(trackOf(rig, verb).outputBus.empty());
}

TEST_CASE("send level, pre and on edit the send to the named bus, and remove deletes it") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const std::string verb = rig.run("track.addBus").at("id");
    rig.run("send.add", {{"track", vox}, {"bus", verb}});
    rig.run("send.setLevel", {{"track", vox}, {"bus", verb}, {"db", -12.0}});
    rig.run("send.setPre", {{"track", vox}, {"bus", verb}});
    rig.run("send.setEnabled", {{"track", vox}, {"bus", verb}, {"value", false}});
    CHECK(trackOf(rig, vox).sends ==
          std::vector<model::Send>{{.bus = model::TrackId{verb}, .levelDb = -12.0, .pre = true, .enabled = false}});
    CHECK(model::toJson(rig.controller->project().document()).at("tracks").at(0).at("sends").size() == 1);

    rig.run("send.remove", {{"track", vox}, {"bus", verb}});
    CHECK(trackOf(rig, vox).sends.empty());
    CHECK_THROWS_AS(rig.run("send.setLevel", {{"track", vox}, {"bus", verb}, {"db", 0.0}}), AppError);
}

TEST_CASE("deleting a bus removes the sends and output buses that name it, and one undo brings all back") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const std::string drums = rig.run("track.addAudio").at("id");
    const std::string verb = rig.run("track.addBus").at("id");
    rig.run("send.add", {{"track", vox}, {"bus", verb}});
    rig.run("track.setOutputBus", {{"track", drums}, {"bus", verb}});

    rig.run("track.remove", {{"track", verb}});
    CHECK(trackOf(rig, vox).sends.empty());
    CHECK(trackOf(rig, drums).outputBus.empty());
    rig.run("project.undo");
    CHECK(trackOf(rig, vox).sends.size() == 1);
    CHECK(trackOf(rig, drums).outputBus == model::TrackId{verb});
}

TEST_CASE("create bus and send makes a bus that the selected track sends to, as one undo step") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const std::string keys = rig.run("track.addMidi").at("id");
    rig.run("track.select", {{"track", vox}});
    const std::string bus = rig.run("send.toNewBus", {{"name", "Verb"}}).at("id");
    CHECK(trackOf(rig, bus).name == "Verb");
    CHECK(trackOf(rig, vox).sends.front().bus == model::TrackId{bus});
    CHECK(rig.nav.selectedTrack().toStdString() == bus);
    CHECK(rig.controller->project().undoName() == "create bus and send");
    rig.run("project.undo");
    CHECK(rig.controller->project().document().tracks.size() == 2);
    CHECK(trackOf(rig, vox).sends.empty());

    const auto tracksBefore = rig.controller->project().document().tracks.size();
    CHECK_THROWS_AS(rig.run("send.toNewBus", {{"tracks", {vox, keys}}}), AppError);
    CHECK(rig.controller->project().document().tracks.size() == tracksBefore);
}
