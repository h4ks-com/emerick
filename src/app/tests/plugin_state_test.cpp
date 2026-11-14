#include <QStringList>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "emerick/app/libraries.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

const std::string kGain = "454D525447414E310000000000000001";

struct PresetRig : ProjectRig {
    PresetRig() {
        registerPluginActions(registry, nav, catalog);
        registerInstrumentActions(registry, *controller, nav, catalog);
        registerTrackPresetActions(registry, *controller, nav, catalog, path("profile/presets"));
        registry.setSetting(kVst3FoldersSetting, Json::array({EMERICK_TEST_VST3_FOLDER}));
    }

    PluginCatalog catalog{{.cacheFolder = path("cache"),
                           .scanner = EMERICK_PLUGIN_SCANNER,
                           .folders = {},
                           .timeout = std::chrono::seconds(2)}};

    std::string addGain(const std::string& track) const {
        return run("insert.add", {{"track", track}, {"plugin", kGain}, {"format", "vst3"}}).at("id");
    }

    double gainOf(const std::string& insert) const {
        return run("insert.params", {{"insert", insert}}).at("params")[0].at("value").get<double>();
    }

    [[nodiscard]] const model::Track& track(const std::string& id) const {
        return *controller->project().track(model::TrackId{id});
    }
};

}  // namespace

TEST_CASE("a plugin insert's state loads into another insert as one undo step and survives a reopen") {
    PresetRig rig;
    const std::string guitar = rig.run("track.addAudio").at("id");
    const std::string amp = rig.addGain(guitar);
    const std::string copy = rig.addGain(guitar);
    rig.run("insert.setParam", {{"insert", amp}, {"param", "Gain"}, {"value", 0.5}});

    const Json read = rig.run("insert.state", {{"insert", amp}});
    CHECK(read.at("plugin").at("id") == kGain);
    REQUIRE_FALSE(read.at("state").get<std::string>().empty());
    rig.run("insert.setState", {{"insert", copy}, {"plugin", kGain}, {"state", read.at("state")}});
    CHECK(rig.controller->project().undoName() == "set Test Gain state");
    CHECK(rig.gainOf(copy) == 0.5);

    rig.run("project.undo");
    rig.controller->flush();
    CHECK(rig.gainOf(copy) == 1.0);
    rig.run("project.redo");
    rig.controller->flush();
    CHECK(rig.gainOf(copy) == 0.5);

    rig.controller->save();
    PresetRig other;
    other.controller->open(rig.controller->folder());
    other.controller->flush();
    CHECK(other.gainOf(copy) == 0.5);
}

TEST_CASE("insert.copyState copies between inserts of one plugin and refuses another plugin or a built-in effect") {
    PresetRig rig;
    const std::string guitar = rig.run("track.addAudio").at("id");
    const std::string amp = rig.addGain(guitar);
    const std::string copy = rig.addGain(guitar);
    const std::string eq = rig.run("insert.add", {{"track", guitar}, {"type", "eq"}}).at("id");
    rig.run("insert.setParam", {{"insert", amp}, {"param", "Gain"}, {"value", 0.25}});

    rig.run("insert.copyState", {{"from", amp}, {"insert", copy}});
    CHECK(rig.gainOf(copy) == 0.25);

    const std::string state = rig.run("insert.state", {{"insert", amp}}).at("state");
    const auto undoName = rig.controller->project().undoName();
    CHECK_THROWS_AS(rig.run("insert.setState", {{"insert", copy}, {"plugin", "org.other.amp"}, {"state", state}}),
                    AppError);
    CHECK_THROWS_AS(rig.run("insert.setState", {{"insert", eq}, {"plugin", kGain}, {"state", state}}), AppError);
    CHECK_THROWS_AS(rig.run("insert.setState", {{"insert", copy}, {"plugin", kGain}, {"state", "not base64!"}}),
                    AppError);
    CHECK_THROWS_AS(rig.run("insert.copyState", {{"from", eq}, {"insert", copy}}), AppError);
    CHECK(rig.controller->project().undoName() == undoName);
    CHECK_FALSE(rig.run("insert.state", {{"insert", eq}}).contains("state"));
}

TEST_CASE("a preset keeps a track's inserts, their plugin states and its trim, and loads them as one undo step") {
    PresetRig rig;
    const std::string guitar = rig.run("track.addAudio").at("id");
    const std::string amp = rig.addGain(guitar);
    const std::string eq = rig.run("insert.add", {{"track", guitar}, {"type", "eq"}}).at("id");
    rig.run("insert.setParam", {{"insert", amp}, {"param", "Gain"}, {"value", 0.5}});
    rig.run("insert.setParam", {{"insert", eq}, {"param", "peak2Gain"}, {"value", 6}});
    rig.run("track.setTrim", {{"track", guitar}, {"db", 3}});
    CHECK(rig.run("track.savePreset", {{"track", guitar}, {"name", "Crunch"}}).at("preset") == "Crunch");
    CHECK(std::filesystem::exists(rig.path("profile/presets/Crunch.json")));
    CHECK(rig.run("track.presets").at("presets") == Json::array({"Crunch"}));

    const std::string bass = rig.run("track.addAudio").at("id");
    rig.run("insert.add", {{"track", bass}, {"type", "delay"}});
    const Json loaded = rig.run("track.loadPreset", {{"track", bass}, {"name", "Crunch"}});
    CHECK(loaded.at("skipped").empty());
    CHECK(rig.controller->project().undoName() == "load preset Crunch");
    const model::Track& track = rig.track(bass);
    REQUIRE(track.inserts.size() == 2);
    CHECK(track.preset == "Crunch");
    CHECK(track.trimDb == 3.0);
    CHECK(track.inserts[0].id.value != amp);
    CHECK(track.inserts[1].type == "eq");
    CHECK(track.inserts[1].params.at("peak2Gain") == 6.0);
    CHECK(rig.gainOf(track.inserts[0].id.value) == 0.5);

    rig.run("project.undo");
    REQUIRE(rig.track(bass).inserts.size() == 1);
    CHECK(rig.track(bass).inserts[0].type == "delay");
    CHECK(rig.track(bass).preset.empty());
}

TEST_CASE("a preset whose plugin is missing loads the rest and raises a notice naming it") {
    PresetRig rig;
    const std::string guitar = rig.run("track.addAudio").at("id");
    rig.addGain(guitar);
    rig.run("insert.add", {{"track", guitar}, {"type", "eq"}});
    rig.run("track.savePreset", {{"track", guitar}, {"name", "Lead"}});
    const std::filesystem::path file = rig.path("profile/presets/Lead.json");
    Json preset = Json::parse(std::ifstream(file));
    preset.at("inserts")[0].at("plugin")["pluginId"] = "org.nowhere.amp";
    preset.at("inserts")[0].at("plugin")["name"] = "Nowhere Amp";
    std::ofstream(file) << preset.dump();

    QStringList notices;
    QObject::connect(&rig.registry, &Registry::noticeRaised,
                     [&](const QString&, const QString& text, const QVariantList&) { notices.append(text); });
    const Json loaded = rig.run("track.loadPreset", {{"track", guitar}, {"name", "Lead"}});
    CHECK(loaded.at("skipped") == Json::array({"Nowhere Amp"}));
    REQUIRE(notices.size() == 1);
    CHECK(notices.front().contains("Nowhere Amp"));
    REQUIRE(rig.track(guitar).inserts.size() == 1);
    CHECK(rig.track(guitar).inserts[0].type == "eq");
}

TEST_CASE("presets live in the profile folder and refuse names that leave it or that Windows cannot store") {
    CHECK(profileSubfolder("/tmp/profile", "presets") == std::filesystem::path{"/tmp/profile/presets"});
    PresetRig rig;
    const std::string guitar = rig.run("track.addAudio").at("id");
    for (const char* name :
         {"",    "..",  "../escape", ".hidden", "a\\b", "a:b",     "a*b", "a?b",  "a\"b",      "a<b",
          "a>b", "a|b", "Clean.",    "Clean ",  "CON",  "con.txt", "Nul", "com1", "LPT9.json", "aux.preset"}) {
        INFO(name);
        CHECK_THROWS_AS(rig.run("track.savePreset", {{"track", guitar}, {"name", name}}), AppError);
    }
    CHECK(rig.run("track.presets").at("presets").empty());
    CHECK_FALSE(std::filesystem::exists(rig.path("profile/escape.json")));
    for (const char* name : {"Console", "COM10", "Lead 2.0"}) {
        rig.run("track.savePreset", {{"track", guitar}, {"name", name}});
    }
    CHECK(rig.run("track.presets").at("presets") == Json::array({"COM10", "Console", "Lead 2.0"}));
    CHECK_THROWS_AS(rig.run("track.loadPreset", {{"track", guitar}, {"name", "Nothing"}}), AppError);
}

TEST_CASE("a preset save that fails keeps the preset saved before") {
    PresetRig rig;
    const std::string guitar = rig.run("track.addAudio").at("id");
    rig.run("track.setTrim", {{"track", guitar}, {"db", 3}});
    rig.run("track.savePreset", {{"track", guitar}, {"name", "Keep"}});
    std::filesystem::create_directory(rig.path("profile/presets/Keep.json.tmp"));
    rig.run("track.setTrim", {{"track", guitar}, {"db", -6}});
    CHECK_THROWS_AS(rig.run("track.savePreset", {{"track", guitar}, {"name", "Keep"}}), AppError);
    CHECK(Json::parse(std::ifstream(rig.path("profile/presets/Keep.json"))).at("trimDb") == 3.0);
}

TEST_CASE("a preset save names why its folder cannot be created") {
    PresetRig rig;
    std::ofstream(rig.path("profile")) << "a file where the folder belongs";
    const std::string guitar = rig.run("track.addAudio").at("id");
    try {
        rig.run("track.savePreset", {{"track", guitar}, {"name", "Clean"}});
        FAIL("the save should fail");
    } catch (const AppError& error) {
        CHECK(std::string{error.what()}.starts_with("cannot create the presets folder"));
    }
}

TEST_CASE("a preset finds its plugin where the scan finds it, when the saved bundle moved") {
    PresetRig rig;
    const std::string guitar = rig.run("track.addAudio").at("id");
    const std::string amp = rig.addGain(guitar);
    const std::string scanned = rig.track(guitar).inserts.front().plugin->bundle;
    rig.run("insert.setParam", {{"insert", amp}, {"param", "Gain"}, {"value", 0.5}});
    rig.run("track.savePreset", {{"track", guitar}, {"name", "Moved"}});
    const std::filesystem::path file = rig.path("profile/presets/Moved.json");
    Json preset = Json::parse(std::ifstream(file));
    preset.at("inserts")[0].at("plugin")["bundle"] = "/no/such/Gain.vst3";
    std::ofstream(file) << preset.dump();

    const std::string bass = rig.run("track.addAudio").at("id");
    CHECK(rig.run("track.loadPreset", {{"track", bass}, {"name", "Moved"}}).at("skipped").empty());
    const model::Insert& loaded = rig.track(bass).inserts.front();
    CHECK(loaded.plugin->bundle == scanned);
    CHECK(rig.gainOf(loaded.id.value) == 0.5);
}
