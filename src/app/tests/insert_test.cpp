#include <sndfile.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "emerick/app/automation_params.hpp"
#include "emerick/app/session_mapping.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;
using Catch::Approx;

namespace {

const model::Track& trackOf(const ProjectRig& rig, const std::string& id) {
    return *rig.controller->project().track(model::TrackId{id});
}

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

}  // namespace

TEST_CASE("an insert adds with its effect's defaults, edits its params and bypass, and each edit undoes in one step") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    const Json added = rig.run("insert.add", {{"track", vox}, {"type", "eq"}});
    const std::string eq = added.at("id");
    CHECK(added.at("track") == vox);
    CHECK(added.at("title") == "Channel EQ");
    CHECK(added.at("params").at("peak2Freq") == 1000.0);
    CHECK(rig.controller->project().undoName() == "add Channel EQ");

    CHECK(rig.run("insert.setParam", {{"insert", eq}, {"param", "peak2Gain"}, {"value", 6}})
              .at("params")
              .at("peak2Gain") == 6.0);
    CHECK(rig.run("insert.setParam", {{"insert", eq}, {"param", "peak2Gain"}, {"value", 99}})
              .at("params")
              .at("peak2Gain") == 24.0);
    CHECK(rig.run("insert.setParam", {{"param", eq + ".lowCutSlope"}, {"value", "36 dB/oct"}})
              .at("params")
              .at("lowCutSlope") == 2.0);
    rig.run("project.undo");
    CHECK(trackOf(rig, vox).inserts.front().params.at("lowCutSlope") == 0.0);
    CHECK_THROWS_AS(rig.run("insert.setParam", {{"insert", eq}, {"param", "wobble"}, {"value", 1}}), AppError);
    CHECK_THROWS_AS(rig.run("insert.setParam", {{"insert", "nope"}, {"param", "peak2Gain"}, {"value", 1}}), AppError);

    CHECK(rig.run("insert.setBypass", {{"insert", eq}}).at("bypass") == true);
    CHECK(rig.run("insert.setBypass", {{"insert", eq}, {"value", false}}).at("bypass") == false);

    const std::string verb = rig.run("insert.add", {{"track", vox}, {"type", "reverb"}, {"index", 0}}).at("id");
    CHECK(trackOf(rig, vox).inserts.front().id == model::InsertId{verb});
    rig.run("insert.move", {{"insert", verb}, {"index", 5}});
    CHECK(trackOf(rig, vox).inserts.back().id == model::InsertId{verb});
    const Json state = projectState(*rig.controller).at("tracks").at(0).at("inserts");
    CHECK(state.at(0).at("id") == eq);
    CHECK(state.at(1).at("type") == "reverb");

    rig.run("insert.remove", {{"insert", eq}});
    CHECK(trackOf(rig, vox).inserts.size() == 1);
    rig.run("project.undo");
    CHECK(trackOf(rig, vox).inserts.size() == 2);

    const std::string keys = rig.run("track.addMidi").at("id");
    CHECK_THROWS_AS(rig.run("insert.add", {{"track", keys}, {"type", "eq"}}), AppError);
    CHECK_THROWS_AS(rig.run("insert.add", {{"track", vox}, {"type", "flanger"}}), AppError);
    const std::string bus = rig.run("track.addBus").at("id");
    CHECK(rig.run("insert.add", {{"track", bus}, {"type", "delay"}}).at("track") == bus);
}

TEST_CASE("the insert editor opens on the insert, and a duplicated track gets its own insert ids") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const std::string eq = rig.run("insert.add", {{"track", vox}, {"type", "eq"}}).at("id");
    rig.run("insert.edit", {{"insert", eq}});
    CHECK(rig.nav.editingInsert().toStdString() == eq);
    rig.run("insert.closeEditor");
    CHECK(rig.nav.editingInsert().isEmpty());

    const std::string copy = rig.run("track.duplicate", {{"track", vox}}).at("id");
    CHECK(trackOf(rig, copy).inserts.size() == 1);
    CHECK(trackOf(rig, copy).inserts.front().id != model::InsertId{eq});
}

TEST_CASE("stems and the mix play each track's inserts before its fader") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    writeConstant(rig.path("half.wav"), 48000, 0.25F);
    rig.run("clip.import", {{"track", vox}, {"path", rig.path("half.wav").string()}, {"at", "1.1.0"}});
    const std::string gain = rig.run("insert.add", {{"track", vox}, {"type", "compressor"}}).at("id");
    rig.run("insert.setParam", {{"insert", gain}, {"param", "threshold"}, {"value", 0}});
    rig.run("insert.setParam", {{"insert", gain}, {"param", "ratio"}, {"value", 1}});
    rig.run("insert.setParam", {{"insert", gain}, {"param", "makeup"}, {"value", 12}});
    rig.run("track.setGain", {{"track", vox}, {"db", -6}});

    rig.run("export.stems", {{"folder", rig.path("stems").string()}});
    rig.run("export.mix", {{"path", rig.path("mix.wav").string()}});
    const float expected = 0.25F * std::pow(10.0F, (12.0F - 6.0F) / 20.0F);
    CHECK(sampleAt(rig.path("stems") / "Vox.wav", 24000) == Approx(expected).margin(1e-3));
    CHECK(sampleAt(rig.path("mix.wav"), 24000) == Approx(expected).margin(1e-3));
}

TEST_CASE("an insert's number and switch params are automation params, and their lanes reach the engine") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const std::string eq = rig.run("insert.add", {{"track", vox}, {"type", "eq"}}).at("id");
    const model::Document& document = rig.controller->project().document();
    const auto params = automationParams(rig.registry, document, trackOf(rig, vox));
    const auto gain = std::ranges::find(params, eq + ".peak2Gain", &AutomationParam::id);
    REQUIRE(gain != params.end());
    CHECK(gain->title == "Channel EQ Mid gain");
    CHECK(gain->maximum == 24.0);
    CHECK(std::ranges::find(params, eq + ".lowCutSlope", &AutomationParam::id) == params.end());

    rig.run("automation.addPoint", {{"track", vox}, {"param", eq + ".peak2Gain"}, {"tick", 0}, {"value", 6.0}});
    const auto lanes = toSession(document, rig.registry, {}).tracks.at(0).automation;
    REQUIRE(lanes.size() == 1);
    CHECK(lanes.front().target == engine::AutomationTarget::insert);
    CHECK(lanes.front().insert == eq);
    CHECK(lanes.front().param == 14);
}
