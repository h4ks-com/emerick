#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>

#include "emerick/app/audio_file.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

struct GeneralMidiRig : ProjectRig {
    GeneralMidiRig() {
        registerPluginActions(registry, nav, catalog);
        registerInstrumentActions(registry, *controller, nav, catalog);
        registry.setSetting(kClapFoldersSetting, Json::array({bundledClapFolder(EMERICK_GM_APP_FOLDER).string()}));
        id = run("track.addInstrument", {{"plugin", kGeneralMidiPluginId}}).at("id").get<std::string>();
        controller->flush();
    }

    PluginCatalog catalog{{.cacheFolder = path("cache"),
                           .scanner = EMERICK_CLAP_SCANNER,
                           .folders = {},
                           .timeout = std::chrono::milliseconds(5000)}};
    std::string id;
};

}  // namespace

TEST_CASE("voices.audition measures each note of a voice and leaves the track and the project as they were") {
    GeneralMidiRig rig;
    const model::Document before = rig.controller->project().document();
    const std::optional<std::string> undo = rig.controller->project().undoName();

    const Json result = rig.run("voices.audition", {{"track", rig.id}, {"voice", "Violin"}});
    CHECK(result.at("voice") == "Violin");
    const Json& notes = result.at("notes");
    REQUIRE(notes.size() == 5);
    CHECK(notes[0].at("note") == 36);
    for (const Json& note : notes) {
        CHECK(note.at("peakDb").is_number());
        CHECK(note.at("peakDb").get<double>() < 0);
        CHECK(note.at("activeRmsDb").is_number());
        CHECK(note.at("harmonicShare").get<double>() > 0.5);
    }
    CHECK(notes[0].at("centroidHz").get<double>() < notes[2].at("centroidHz").get<double>());
    CHECK(notes[2].at("centroidHz").get<double>() < notes[4].at("centroidHz").get<double>());

    CHECK(rig.controller->project().document() == before);
    CHECK(rig.controller->project().undoName() == undo);
    CHECK(rig.run("instrument.params", {{"track", rig.id}}).at("params")[0].at("text") == "Acoustic Grand Piano");
}

TEST_CASE("voices.audition plays the track's current voice by default, writes a WAV and names an unknown voice") {
    GeneralMidiRig rig;
    const std::filesystem::path file = rig.path("audition.wav");
    const Json result =
        rig.run("voices.audition",
                {{"track", rig.id}, {"notes", {60}}, {"seconds", 0.5}, {"path", file.string()}, {"play", true}});
    CHECK(result.at("voice") == "Acoustic Grand Piano");
    CHECK(result.at("notes").size() == 1);
    const AudioData audio = readAudio(file);
    REQUIRE(audio.channels.size() == 2);
    CHECK(audio.channels[0].size() == 24000);
    CHECK(rig.controller->engine().previewPlaying());

    CHECK_THROWS_AS(rig.run("voices.audition", {{"track", rig.id}, {"voice", "No Such Voice"}}), AppError);
    CHECK_THROWS_AS(rig.run("voices.audition", {{"track", rig.id}, {"notes", {200}}}), AppError);
}
