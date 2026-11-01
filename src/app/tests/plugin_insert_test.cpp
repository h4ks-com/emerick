#include <sndfile.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "emerick/app/session_mapping.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;
using Catch::Approx;

namespace {

const std::string kGain = "454D525447414E310000000000000001";

struct PluginInsertRig : ProjectRig {
    PluginInsertRig() {
        registerPluginActions(registry, nav, catalog);
        registerInstrumentActions(registry, *controller, nav, catalog);
        registry.setSetting(kVst3FoldersSetting, Json::array({EMERICK_TEST_VST3_FOLDER}));
    }

    // The test folder holds a bundle that hangs, so a short timeout keeps each scan quick.
    PluginCatalog catalog{{.cacheFolder = path("cache"),
                           .scanner = EMERICK_PLUGIN_SCANNER,
                           .folders = {},
                           .timeout = std::chrono::milliseconds(500)}};

    // An audio track playing a constant 0.5 with the test gain as its insert.
    std::pair<std::string, std::string> guitarWithGain() {
        const std::string guitar = run("track.addAudio", {{"name", "Guitar"}}).at("id");
        writeConstant(path("dc.wav"), 48000, 0.5F);
        run("clip.import", {{"track", guitar}, {"path", path("dc.wav").string()}, {"at", "1.1.0"}});
        const std::string amp = run("insert.add", {{"track", guitar}, {"plugin", kGain}, {"format", "vst3"}}).at("id");
        return {guitar, amp};
    }

    float mixAt(core::Frames frame) {
        const std::filesystem::path mix = path("mix.wav");
        run("export.mix", {{"path", mix.string()}});
        SF_INFO info{};
        SNDFILE* file = sf_open(mix.c_str(), SFM_READ, &info);
        REQUIRE(file != nullptr);
        std::vector<float> samples(static_cast<std::size_t>(info.channels));
        sf_seek(file, frame, SEEK_SET);
        sf_readf_float(file, samples.data(), 1);
        sf_close(file);
        return samples.at(0);
    }
};

}  // namespace

TEST_CASE("plugins.list lists a VST3 effect and blocks the VST3 bundles that crash or hang") {
    PluginInsertRig rig;
    const Json listed = rig.run("plugins.list");
    const Json& plugins = listed.at("plugins");
    const auto gain = std::ranges::find_if(plugins, [](const Json& plugin) {
        return plugin.at("id") == kGain && plugin.at("bundle").get<std::string>().contains("gain");
    });
    REQUIRE(gain != plugins.end());
    CHECK(gain->at("format") == "vst3");
    CHECK(gain->at("effect") == true);
    CHECK(gain->at("instrument") == false);
    CHECK(std::ranges::any_of(
        plugins, [](const Json& plugin) { return plugin.at("bundle").get<std::string>().contains("described"); }));
    const Json& blocked = listed.at("blocked");
    CHECK(blocked.size() == 2);
    CHECK(
        std::ranges::any_of(blocked, [](const Json& entry) { return entry.at("reason") == "crashed while loading"; }));
    CHECK(listed.at("message").get<std::string>().starts_with("Found 0 CLAP and 2 VST3 plugins"));
}

TEST_CASE("a VST3 insert plays in the mix, takes params by name and choice text, and bypass passes the audio") {
    PluginInsertRig rig;
    const auto [guitar, amp] = rig.guitarWithGain();
    CHECK(rig.controller->project().undoName() == "add Test Gain");
    const Json listed = rig.run("insert.params", {{"insert", amp}});
    REQUIRE(listed.at("params").size() == 2);
    CHECK(listed.at("params")[1].at("choices") == Json::array({"Normal", "Inverted"}));
    CHECK(rig.mixAt(24000) == Approx(0.5F).margin(1e-4));

    const Json set = rig.run("insert.setParam", {{"insert", amp}, {"param", "gain"}, {"value", 0.5}});
    CHECK(set.at("param") == "Gain");
    CHECK(set.at("text") == "0.50x");
    CHECK(set.at("params").at("Gain") == 0.5);
    CHECK(rig.mixAt(24000) == Approx(0.25F).margin(1e-4));
    rig.run("insert.setParam", {{"param", amp + ".Polarity"}, {"value", "Inverted"}});
    CHECK(rig.mixAt(24000) == Approx(-0.25F).margin(1e-4));
    CHECK_THROWS_AS(rig.run("insert.setParam", {{"insert", amp}, {"param", "Drive"}, {"value", 1}}), AppError);
    CHECK_THROWS_AS(rig.run("insert.setParam", {{"insert", amp}, {"param", "Gain"}, {"value", 9}}), AppError);

    rig.run("insert.setBypass", {{"insert", amp}, {"value", true}});
    CHECK(rig.mixAt(24000) == Approx(0.5F).margin(1e-4));
    rig.run("insert.setBypass", {{"insert", amp}, {"value", false}});
    for (int step = 0; step < 3; ++step) {
        rig.run("project.undo");
    }
    rig.controller->flush();
    CHECK(rig.run("insert.params", {{"insert", amp}}).at("params")[1].at("value") == 0.0);
    CHECK(rig.mixAt(24000) == Approx(0.25F).margin(1e-4));
}

TEST_CASE("a VST3 insert's state saves with the project and loads back") {
    PluginInsertRig rig;
    const auto [guitar, amp] = rig.guitarWithGain();
    rig.run("insert.setParam", {{"insert", amp}, {"param", "Gain"}, {"value", 1.5}});
    rig.controller->save();

    PluginInsertRig other;
    other.controller->open(rig.controller->folder());
    other.controller->flush();
    const model::Track& track = *other.controller->project().track(model::TrackId{guitar});
    REQUIRE(track.inserts.size() == 1);
    REQUIRE(track.inserts.front().plugin.has_value());
    CHECK(track.inserts.front().plugin->format == core::PluginFormat::vst3);
    const auto plugin = other.controller->instruments().find(model::InsertId{amp});
    REQUIRE(plugin != nullptr);
    CHECK(plugin->paramValue(1) == 1.5);
}

TEST_CASE("a VST3 insert param is an automation param whose lane moves the plugin") {
    PluginInsertRig rig;
    const auto [guitar, amp] = rig.guitarWithGain();
    rig.run("automation.addPoint", {{"track", guitar}, {"param", amp + ".Gain"}, {"tick", 0}, {"value", 0.5}});
    const model::Track& track = *rig.controller->project().track(model::TrackId{guitar});
    const auto lanes = pluginLanes(track, rig.controller->instruments().insertPlugins());
    REQUIRE(lanes.size() == 1);
    CHECK(lanes.front().param == 1);
    CHECK(lanes.front().points.front().value == 0.25F);
    CHECK(rig.mixAt(24000) == Approx(0.25F).margin(1e-4));
}

TEST_CASE("a missing VST3 insert stays in the project, warns on its track and stops an export") {
    PluginInsertRig rig;
    const auto [guitar, amp] = rig.guitarWithGain();
    rig.controller->project().editTrack(model::TrackId{guitar}, "move plugin", [](model::Track& edited) {
        edited.inserts.front().plugin->bundle = "/no/such/Gain.vst3";
    });
    rig.controller->flush();
    CHECK(
        rig.controller->trackErrors().value(QString::fromStdString(guitar)).toString().startsWith("Install Test Gain"));
    CHECK_THROWS_AS(rig.run("insert.params", {{"insert", amp}}), AppError);
    CHECK_THROWS_AS(rig.mixAt(24000), AppError);
    REQUIRE(rig.controller->project().track(model::TrackId{guitar})->inserts.front().plugin.has_value());
}
