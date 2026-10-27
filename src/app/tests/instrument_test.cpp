#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <span>
#include <vector>

#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "emerick/model/json.hpp"
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

TEST_CASE("track.addInstrument adds a MIDI track that plays its clips through the instrument into its audio output") {
    InstrumentRig rig;
    const model::TrackId id = rig.addSynth();
    const model::Track& track = *rig.controller->project().track(id);
    CHECK(track.kind == "midi");
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

TEST_CASE("a MIDI track sends MIDI out until it loads an instrument, which plays it into the default audio output") {
    InstrumentRig rig;
    const model::TrackId id{rig.run("track.addMidi", {{"midiOut", {{"port", kMidiPort}}}}).at("id").get<std::string>()};
    CHECK(rig.controller->project().track(id)->audioOut.deviceId.empty());
    addNote(*rig.controller, id);
    try {
        rig.run("insert.add", {{"track", id.value}, {"type", "eq"}});
        FAIL("a MIDI track without an instrument took an insert");
    } catch (const AppError& error) {
        CHECK(std::string{error.what()}.ends_with("load an instrument on this MIDI track first"));
    }

    rig.controller->play();
    rig.audio->process(kBlock * 64);
    CHECK(std::ranges::any_of(rig.midi->sent(kMidiPort), [](const engine::SentMidi& message) {
        return message.bytes.size() == 3 && message.bytes[0] == 0x90 && message.bytes[1] == 69;
    }));
    CHECK(loudest(rig.audio->output("interface", 1)) == 0.0F);
    rig.controller->stop();

    rig.run("instrument.load", {{"track", id.value}, {"plugin", kSine}});
    CHECK(rig.controller->project().track(id)->audioOut.deviceId == "interface");
    rig.controller->flush();
    rig.controller->locate(0);
    rig.controller->play();
    rig.audio->process(kBlock * 64);
    CHECK(loudest(rig.audio->output("interface", 1)) > 0.4F);
}

TEST_CASE("a piano roll key plays through the track's instrument until its note-off") {
    InstrumentRig rig;
    const model::TrackId id = rig.addSynth();
    rig.controller->flush();
    rig.controller->engine().keepWarm(true);
    const Json key = {{"track", id.value}, {"pitch", 69}};
    const Json played = rig.run("midi.audition", key);
    CHECK(played.at("sent") == true);
    CHECK(played.at("instrument") == true);
    const auto held = static_cast<std::size_t>(rig.audio->processedFrames());
    rig.audio->process(kBlock * 16);
    rig.run("midi.audition", {{"track", id.value}, {"pitch", 69}, {"on", false}});
    const auto released = static_cast<std::size_t>(rig.audio->processedFrames());
    rig.audio->process(kBlock * 16);

    const std::vector<float> output = rig.audio->output("interface", 1);
    CHECK(loudest({output.begin() + static_cast<std::ptrdiff_t>(held),
                   output.begin() + static_cast<std::ptrdiff_t>(released)}) > 0.4F);
    CHECK(loudest({output.begin() + static_cast<std::ptrdiff_t>(released), output.end()}) == 0.0F);
}

TEST_CASE("instrument.remove clears the slot and the track plays no audio again") {
    InstrumentRig rig;
    const model::TrackId id = rig.addSynth();
    rig.controller->flush();
    REQUIRE(rig.controller->instruments().find(id) != nullptr);
    const Json bus = rig.run("track.addBus").at("id");
    rig.run("instrument.remove", {{"track", id.value}});
    rig.controller->flush();
    CHECK_FALSE(rig.controller->project().track(id)->instrument.has_value());
    CHECK(rig.controller->instruments().find(id) == nullptr);
    try {
        rig.run("instrument.showEditor", {{"track", id.value}});
        FAIL("the editor of a removed instrument opened");
    } catch (const AppError& error) {
        CHECK(std::string{error.what()}.ends_with("load an instrument on this MIDI track first"));
    }
    CHECK_THROWS_AS(rig.run("send.add", {{"track", id.value}, {"bus", bus}}), AppError);
    rig.run("project.undo");
    CHECK(rig.controller->project().track(id)->instrument.has_value());
}

TEST_CASE("a project saves its instrument's current state and loads it back into the instrument") {
    InstrumentRig rig;
    const model::TrackId id = rig.addSynth();
    rig.controller->flush();
    REQUIRE(rig.controller->instruments().find(id)->loadState(levelState(0.25F)));
    rig.controller->save();
    std::ifstream file(rig.controller->folder() / "project.json");
    const Json saved = Json::parse(file);
    CHECK(saved.at("format") == model::kFormatVersion);
    CHECK(saved.at("tracks")[0].at("instrument").at("state") == "AACAPg==");

    InstrumentRig other;
    other.controller->open(rig.controller->folder());
    other.controller->flush();
    const auto reopened = other.controller->instruments().find(id);
    REQUIRE(reopened != nullptr);
    CHECK(reopened->saveState() == levelState(0.25F));
}

TEST_CASE("a format 10 instrument track loads as a MIDI track with its instrument and state") {
    InstrumentRig rig;
    const model::TrackId id = rig.addSynth();
    rig.controller->flush();
    REQUIRE(rig.controller->instruments().find(id)->loadState(levelState(0.25F)));
    rig.controller->save();
    const std::filesystem::path file = rig.controller->folder() / "project.json";
    Json saved = Json::parse(std::ifstream(file));
    saved.at("tracks")[0]["kind"] = "instrument";
    std::ofstream(file) << saved.dump();

    InstrumentRig other;
    other.controller->open(rig.controller->folder());
    other.controller->flush();
    const model::Track& track = *other.controller->project().track(id);
    CHECK(track.kind == "midi");
    REQUIRE(track.instrument.has_value());
    CHECK(track.instrument->pluginId == kSine);
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
    CHECK_THROWS_AS(rig.run("instrument.load", {{"track", rig.run("track.addAudio").at("id")}, {"plugin", kSine}}),
                    AppError);
}

TEST_CASE("a new General MIDI instrument plays the SoundFont the setting names, and keeps it in its state") {
    InstrumentRig rig;
    rig.registry.setSetting(kClapFoldersSetting, Json::array({bundledClapFolder(EMERICK_GM_APP_FOLDER).string()}));
    const std::filesystem::path soundFont = rig.path("Mine.sf2");
    std::ofstream{soundFont} << "sf2";
    rig.registry.setSetting(kGeneralMidiSoundFontSetting, soundFont.string());
    const Json added = rig.run("track.addInstrument", {{"plugin", kGeneralMidiPluginId}});
    const model::Track& track = *rig.controller->project().track(model::TrackId{added.at("id").get<std::string>()});
    REQUIRE(track.instrument.has_value());
    CHECK(track.name == "General MIDI");
    const std::string state(track.instrument->state.begin(), track.instrument->state.end());
    CHECK(std::filesystem::equivalent(state, soundFont));
}
