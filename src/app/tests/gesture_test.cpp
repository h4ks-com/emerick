#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>
#include <vector>

#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

QVariantList setGain(const std::string& track, double db) {
    return {QVariantMap{{"id", "track.setGain"}, {"params", QVariantMap{{"track", qs(track)}, {"db", db}}}}};
}

double gainOf(const ProjectRig& rig, const std::string& track) {
    return rig.controller->project().track(model::TrackId{track})->gainDb;
}

struct ActionLog {
    explicit ActionLog(const Registry& registry) {
        QObject::connect(
            &registry, &Registry::actionRan,
            [this](const std::string& id, const Json& params, ActionSource) { lines.push_back({id, params}); });
    }
    std::vector<std::pair<std::string, Json>> lines;
};

}  // namespace

TEST_CASE("a gesture runs its first update at once and the latest one each frame, as one undo step and log line") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const ActionLog log(rig.registry);

    rig.registry.updateGesture("Set track gain", setGain(vox, -1.0));
    CHECK(gainOf(rig, vox) == -1.0);
    CHECK(rig.registry.gesturing());
    for (const double db : {-2.0, -3.0, -4.0}) {
        rig.registry.updateGesture("Set track gain", setGain(vox, db));
    }
    CHECK(processUntil([&] { return gainOf(rig, vox) == -4.0; }));
    CHECK(log.lines.empty());

    rig.registry.updateGesture("Set track gain", setGain(vox, -5.0));
    rig.registry.endGesture();
    CHECK(gainOf(rig, vox) == -5.0);
    CHECK_FALSE(rig.registry.gesturing());
    REQUIRE(log.lines.size() == 1);
    CHECK(log.lines.front().first == "track.setGain");
    CHECK(log.lines.front().second == Json{{"track", vox}, {"db", -5.0}});
    CHECK(rig.controller->project().undoName() == "set track gain");

    rig.run("project.undo");
    CHECK(gainOf(rig, vox) == 0.0);
    CHECK(rig.controller->project().undoName() == "add track Audio 1");
}

TEST_CASE("a cancelled gesture reverts, ignores the rest of the drag and logs nothing") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const ActionLog log(rig.registry);

    rig.registry.updateGesture("Set track gain", setGain(vox, -6.0));
    rig.registry.cancelGesture();
    CHECK(gainOf(rig, vox) == 0.0);
    rig.registry.updateGesture("Set track gain", setGain(vox, -7.0));
    CHECK(gainOf(rig, vox) == 0.0);
    rig.registry.endGesture();
    CHECK(log.lines.empty());
    CHECK(rig.controller->project().undoName() == "add track Audio 1");

    rig.registry.updateGesture("Set track gain", setGain(vox, -8.0));
    CHECK(gainOf(rig, vox) == -8.0);
    rig.registry.endGesture();
}

TEST_CASE("a gesture that ends where it started leaves no undo step and logs nothing") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const ActionLog log(rig.registry);

    rig.registry.updateGesture("Set track gain", setGain(vox, -6.0));
    rig.registry.updateGesture("Set track gain", setGain(vox, 0.0));
    rig.registry.endGesture();
    CHECK(gainOf(rig, vox) == 0.0);
    CHECK(log.lines.empty());
    CHECK(rig.controller->project().undoName() == "add track Audio 1");
}
