#include "emerick/model/automation.hpp"

#include <sndfile.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <vector>

#include "emerick/app/session_mapping.hpp"
#include "emerick/engine/sound_file.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;
using Catch::Approx;

namespace {

const model::Track& trackOf(const ProjectRig& rig, const std::string& id) {
    return *rig.controller->project().track(model::TrackId{id});
}

const model::AutomationLane* laneOf(const ProjectRig& rig, const std::string& track, const std::string& param) {
    return model::findLane(trackOf(rig, track), param);
}

QVariantList gesture(const std::string& action, const std::string& track, const char* key, double value) {
    return {QVariantMap{{"id", qs(action)}, {"params", QVariantMap{{"track", qs(track)}, {key, value}}}}};
}

float sampleAt(const std::filesystem::path& path, core::Frames frame) {
    SF_INFO info{};
    SNDFILE* file = engine::openSoundFile(path, SFM_READ, &info);
    REQUIRE(file != nullptr);
    std::vector<float> samples(static_cast<std::size_t>(info.channels));
    sf_seek(file, frame, SEEK_SET);
    sf_readf_float(file, samples.data(), 1);
    sf_close(file);
    return samples.at(0);
}

// Moves the gain fader through values while the transport plays, one gesture update every few milliseconds.
void moveFader(ProjectRig& rig, const std::string& track, const std::vector<double>& values) {
    for (const double db : values) {
        rig.registry.updateGesture("Set track gain", gesture("track.setGain", track, "db", db));
        processUntil([] { return false; }, 4);
    }
}

std::vector<double> sweep(double from, double to, int steps) {
    std::vector<double> values;
    for (int step = 0; step <= steps; ++step) {
        values.push_back(from + ((to - from) * step / steps));
    }
    return values;
}

}  // namespace

TEST_CASE("points add, move, change curve and delete, each as one undo step, and the state lists the lane") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    const Json added = rig.run("automation.addPoint", {{"track", vox}, {"tick", 960}, {"value", -6.0}});
    const std::string first = added.at("point");
    CHECK(added.at("param") == "gain");
    CHECK(rig.controller->project().undoName() == "add automation point");
    CHECK(rig.nav.selectedPoints() == QStringList{qs(first)});
    const std::string second =
        rig.run("automation.addPoint", {{"track", vox}, {"param", "pan"}, {"tick", 0}, {"value", 0.5}}).at("point");

    rig.run("automation.movePoints", {{"points", {first}}, {"by", 480}, {"delta", 100.0}});
    CHECK(laneOf(rig, vox, "gain")->points.at(0).tick == 1440);
    CHECK(laneOf(rig, vox, "gain")->points.at(0).value == 6.0);
    rig.run("project.undo");
    CHECK(laneOf(rig, vox, "gain")->points.at(0).tick == 960);

    rig.run("automation.setCurve", {{"points", {first, second}}, {"curve", "hold"}});
    CHECK(laneOf(rig, vox, "pan")->points.at(0).curve == core::AutomationCurve::hold);
    CHECK(rig.controller->project().undoName() == "set automation curve");

    const Json track = projectState(*rig.controller).at("tracks").at(0);
    CHECK(track.at("automationMode") == "read");
    CHECK(track.at("automation") ==
          Json::array({{{"param", "gain"}, {"pointCount", 1}}, {{"param", "pan"}, {"pointCount", 1}}}));
    CHECK(rig.run("automation.getLane", {{"track", vox}, {"includePoints", false}}).at("pointCount") == 1);

    rig.nav.setSelectedPoints({qs(first)});
    rig.run("edit.delete");
    CHECK(laneOf(rig, vox, "gain")->points.empty());
    CHECK(trackOf(rig, vox).clips.empty());
    rig.run("automation.clearLane", {{"track", vox}, {"param", "pan"}});
    CHECK(laneOf(rig, vox, "pan") == nullptr);
}

TEST_CASE("automation refuses values out of range, unknown parameters and tracks that play no audio") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const std::string keys = rig.run("track.addMidi").at("id");
    const model::Document before = rig.controller->project().document();
    CHECK_THROWS_AS(rig.run("automation.addPoint", {{"track", vox}, {"tick", 0}, {"value", 12.0}}), AppError);
    CHECK_THROWS_AS(
        rig.run("automation.addPoint", {{"track", vox}, {"param", "send.x.level"}, {"tick", 0}, {"value", 0.0}}),
        AppError);
    CHECK_THROWS_AS(rig.run("automation.addPoint", {{"track", keys}, {"tick", 0}, {"value", 0.0}}), AppError);
    CHECK_THROWS_AS(rig.run("automation.setMode", {{"track", vox}, {"mode", "write"}}), AppError);
    CHECK_THROWS_AS(rig.run("automation.deletePoints", {{"points", {"nope"}}}), AppError);
    CHECK(rig.controller->project().document() == before);

    rig.run("automation.showLane", {{"track", vox}, {"param", "mute"}});
    CHECK(rig.nav.automationOpen());
    CHECK(rig.nav.automationLane(qs(vox)) == "mute");
    CHECK(rig.run("view.toggleAutomation").at("automationOpen") == false);
}

TEST_CASE("the engine plays lanes in Read and ignores them in Off, and a send lane follows its send") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const std::string verb = rig.run("track.addBus").at("id");
    rig.run("send.add", {{"track", vox}, {"bus", verb}});
    rig.run("automation.addPoint", {{"track", vox}, {"tick", 0}, {"value", -6.0}});
    rig.run("automation.addPoint",
            {{"track", vox}, {"param", "send." + verb + ".level"}, {"tick", 0}, {"value", -12.0}});
    const auto lanes = [&] {
        return toSession(rig.controller->project().document(), rig.registry, {}).tracks.at(0).automation;
    };
    REQUIRE(lanes().size() == 2);
    CHECK(lanes().at(0).target == engine::AutomationTarget::gain);
    CHECK(lanes().at(1).target == engine::AutomationTarget::send);
    CHECK(lanes().at(1).bus == verb);

    rig.run("automation.setMode", {{"track", vox}, {"mode", "off"}});
    CHECK(lanes().empty());
    rig.run("automation.setMode", {{"track", vox}, {"mode", "read"}});
    rig.run("track.remove", {{"track", verb}});
    CHECK(trackOf(rig, vox).automation.size() == 1);
}

TEST_CASE("an exported stem plays the gain automation") {
    ProjectRig rig;
    const std::string pad = rig.run("track.addAudio", {{"name", "Pad"}}).at("id");
    writeConstant(rig.path("ones.wav"), 96000, 1.0F);
    rig.run("clip.import", {{"track", pad}, {"path", rig.path("ones.wav").string()}, {"at", 0}});
    rig.run("automation.addPoint", {{"track", pad}, {"tick", 0}, {"value", -24.0}});
    rig.run("automation.addPoint", {{"track", pad}, {"tick", 1920}, {"value", 0.0}});

    rig.run("export.stems", {{"folder", rig.path("stems").string()}});
    const auto at = [&](core::Ticks ticks) {
        const float sample = sampleAt(rig.path("stems") / "Pad.wav",
                                      rig.controller->project().document().tempo.ticksToFrames(ticks, kRate));
        return 20.0 * std::log10(static_cast<double>(sample));
    };
    CHECK(at(480) == Approx(-18.0).margin(0.05));
    CHECK(at(960) == Approx(-12.0).margin(0.05));
    CHECK(at(2400) == Approx(0.0).margin(0.05));
}

TEST_CASE("Touch writes a moved fader as thinned points in one undo step and leaves the track gain alone") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    rig.run("automation.setMode", {{"track", vox}, {"mode", "touch"}});
    rig.run("transport.play");
    const std::vector<double> values = sweep(-20.0, -10.0, 40);
    moveFader(rig, vox, values);
    CHECK(processUntil([&] { return rig.controller->automatedValue(trackOf(rig, vox), "gain") == values.back(); }));
    rig.registry.endGesture();
    rig.run("transport.stop");

    const model::AutomationLane* lane = laneOf(rig, vox, "gain");
    REQUIRE(lane != nullptr);
    CHECK(lane->points.size() > 2);
    CHECK(lane->points.size() < values.size());
    CHECK(lane->points.front().value == Approx(-20.0));
    CHECK(lane->points.back().value == Approx(-10.0));
    CHECK(std::ranges::is_sorted(lane->points, {}, &model::AutomationPoint::tick));
    CHECK(trackOf(rig, vox).gainDb == 0.0);
    CHECK(rig.controller->project().undoName() == "write automation");
    rig.run("project.undo");
    CHECK(trackOf(rig, vox).automation.empty());
}

TEST_CASE("Latch keeps writing the last value after the fader is let go until the transport stops") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    rig.run("automation.setMode", {{"track", vox}, {"mode", "latch"}});
    rig.run("transport.play");
    moveFader(rig, vox, sweep(-3.0, -9.0, 10));
    rig.registry.endGesture();
    CHECK(laneOf(rig, vox, "gain") == nullptr);
    processUntil([] { return false; }, 60);
    const core::Ticks released = rig.controller->positionTicks();
    rig.run("transport.stop");

    const model::AutomationLane* lane = laneOf(rig, vox, "gain");
    REQUIRE(lane != nullptr);
    CHECK(lane->points.back().value == Approx(-9.0));
    CHECK(lane->points.back().tick >= released);
    CHECK(rig.controller->project().undoName() == "write automation");
}

TEST_CASE("in Read a fader move during playback edits the track gain, as no lane is written") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    rig.run("transport.play");
    moveFader(rig, vox, {-4.0, -5.0});
    rig.registry.endGesture();
    rig.run("transport.stop");
    CHECK(trackOf(rig, vox).gainDb == -5.0);
    CHECK(trackOf(rig, vox).automation.empty());
}
