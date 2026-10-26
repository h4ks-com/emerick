#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <ranges>
#include <span>
#include <vector>

#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

const std::string kSine = "org.emerick.test.sine";

struct InstrumentRig : ProjectRig {
    InstrumentRig() {
        registerPluginActions(registry, nav, catalog);
        registerInstrumentActions(registry, *controller, nav, catalog);
        registry.setSetting(kClapFoldersSetting, Json::array({EMERICK_TEST_CLAP_FOLDER}));
    }

    model::TrackId addSynth() {
        const Json added = run("track.addInstrument", {{"plugin", kSine}});
        return model::TrackId{added.at("id").get<std::string>()};
    }

    // The test folder holds a bundle that hangs, so a short timeout keeps each scan quick.
    PluginCatalog catalog{{.cacheFolder = path("cache"),
                           .scanner = EMERICK_CLAP_SCANNER,
                           .folders = {},
                           .timeout = std::chrono::milliseconds(500)}};
};

std::vector<std::uint8_t> levelState(float level) {
    const auto bytes = std::bit_cast<std::array<std::uint8_t, sizeof(float)>>(level);
    return {bytes.begin(), bytes.end()};
}

void addNote(ProjectController& controller, const model::TrackId& id) {
    controller.project().editTrack(id, "add note", [](model::Track& track) {
        model::MidiClip clip;
        clip.length = 960;
        clip.notes = {{.pitch = 69, .velocity = 100, .start = 0, .length = 480, .channel = 1}};
        track.clips.emplace_back(clip);
    });
    controller.flush();
}

float loudest(const std::vector<float>& samples) {
    return std::ranges::max(samples | std::views::transform([](float sample) { return std::abs(sample); }));
}

}  // namespace

TEST_CASE("an instrument track plays its MIDI clips through the CLAP instrument into its audio output") {
    InstrumentRig rig;
    const model::TrackId id = rig.addSynth();
    const model::Track& track = *rig.controller->project().track(id);
    CHECK(track.kind == "instrument");
    CHECK(track.name == "Test Sine");
    REQUIRE(track.instrument.has_value());
    CHECK(track.instrument->pluginId == kSine);
    addNote(*rig.controller, id);
    CHECK(rig.controller->trackErrors().value(QString::fromStdString(id.value)).toString().isEmpty());

    rig.controller->play();
    rig.audio->process(kBlock * 64);
    CHECK(loudest(rig.audio->output("interface", 1)) > 0.4F);
    CHECK(rig.run("send.add", {{"track", id.value}, {"bus", rig.run("track.addBus").at("id")}}).contains("sends"));
}

TEST_CASE("a project saves its instrument's current state and loads it back into the instrument") {
    InstrumentRig rig;
    const model::TrackId id = rig.addSynth();
    rig.controller->flush();
    REQUIRE(rig.controller->instruments().find(id)->loadState(levelState(0.25F)));
    rig.controller->save();
    std::ifstream file(rig.controller->folder() / "project.json");
    const Json saved = Json::parse(file);
    CHECK(saved.at("format") == 10);
    CHECK(saved.at("tracks")[0].at("instrument").at("state") == "AACAPg==");

    InstrumentRig other;
    other.controller->open(rig.controller->folder());
    other.controller->flush();
    const auto reopened = other.controller->instruments().find(id);
    REQUIRE(reopened != nullptr);
    CHECK(reopened->saveState() == levelState(0.25F));
}

TEST_CASE("a missing instrument keeps its state and shows why the track is silent") {
    InstrumentRig rig;
    const model::TrackId id = rig.addSynth();
    rig.controller->project().editTrack(id, "move plugin", [](model::Track& track) {
        track.instrument->bundle = "/no/such/Synth.clap";
        track.instrument->state = levelState(0.75F);
    });
    rig.controller->flush();
    CHECK(rig.controller->instruments().find(id) == nullptr);
    const QString error = rig.controller->trackErrors().value(QString::fromStdString(id.value)).toString();
    CHECK(error.startsWith("Install Test Sine or load another instrument"));
    CHECK_THROWS_AS(rig.run("instrument.showEditor", {{"track", id.value}}), AppError);

    rig.controller->save();
    std::ifstream file(rig.controller->folder() / "project.json");
    CHECK(Json::parse(file).at("tracks")[0].at("instrument").at("state") == "AABAPw==");
}

TEST_CASE("showing the editor of an instrument without one says so") {
    InstrumentRig rig;
    const model::TrackId id = rig.addSynth();
    rig.controller->flush();
    try {
        rig.run("instrument.showEditor", {{"track", id.value}});
        FAIL("the editor opened");
    } catch (const AppError& error) {
        CHECK(std::string{error.what()} == "Test Sine has no editor");
    }
    CHECK_THROWS_AS(rig.run("instrument.load", {{"track", rig.run("track.addMidi").at("id")}, {"plugin", kSine}}),
                    AppError);
}
