#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>

#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "emerick/app/project_state.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

struct SourceRig : ProjectRig {
    SourceRig() {
        registerPluginActions(registry, nav, catalog);
        registerInstrumentActions(registry, *controller, nav, catalog);
        registry.setSetting(kClapFoldersSetting, Json::array({EMERICK_TEST_CLAP_FOLDER}));
    }

    std::string addDevice() {
        const std::string id =
            run("track.addInstrument", {{"plugin", "org.emerick.test.sine"}, {"name", "Style"}}).at("id");
        controller->project().editTrack(model::TrackId{id}, "add notes", [](model::Track& track) {
            model::MidiClip clip;
            clip.length = 1920;
            clip.notes = {{.pitch = 60, .velocity = 100, .start = 480, .length = 240, .channel = 1},
                          {.pitch = 62, .velocity = 90, .start = 960, .length = 240, .channel = 2}};
            track.clips.emplace_back(clip);
        });
        controller->flush();
        return id;
    }

    // The test folder holds a bundle that hangs, so a short timeout keeps each scan quick.
    PluginCatalog catalog{{.cacheFolder = path("cache"),
                           .scanner = EMERICK_CLAP_SCANNER,
                           .folders = {},
                           .timeout = std::chrono::milliseconds(500)}};
};

}  // namespace

TEST_CASE("a MIDI input takes a device's output in place of its port, and undo gives the port back") {
    SourceRig rig;
    const std::string device = rig.addDevice();
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    const QVariantList sources = rig.controller->midiSources();
    REQUIRE(sources.size() == 1);
    CHECK(sources[0].toMap().value("name").toString() == "Style / Test Sine");
    CHECK(sources[0].toMap().value("id").toString().toStdString() == device);

    const Json midiIn = rig.run("track.setMidiIn", {{"track", keys}, {"sourceTrack", device}}).at("midiIn");
    CHECK(midiIn.at("sourceTrack") == device);
    CHECK(midiIn.at("port") == "");
    CHECK(rig.run("track.setMidiIn", {{"track", keys}, {"channel", 2}}).at("midiIn").at("sourceTrack") == device);

    rig.run("project.undo");
    rig.run("project.undo");
    CHECK(rig.controller->project().track(model::TrackId{keys})->midiIn.portId == kMidiPort);
    CHECK(rig.controller->project().track(model::TrackId{keys})->midiIn.sourceTrack.empty());
    rig.run("project.redo");
    CHECK(rig.controller->project().track(model::TrackId{keys})->midiIn.sourceTrack.value == device);

    rig.run("track.setMidiIn", {{"track", keys}, {"port", kMidiPort}});
    CHECK(rig.controller->project().track(model::TrackId{keys})->midiIn.sourceTrack.empty());
}

TEST_CASE("a MIDI input refuses a track without an instrument, itself and a loop") {
    SourceRig rig;
    const std::string device = rig.addDevice();
    const std::string other = rig.addDevice();
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    CHECK_THROWS_AS(rig.run("track.setMidiIn", {{"track", device}, {"sourceTrack", keys}}), AppError);
    CHECK_THROWS_AS(rig.run("track.setMidiIn", {{"track", device}, {"sourceTrack", device}}), AppError);
    rig.run("track.setMidiIn", {{"track", other}, {"sourceTrack", device}});
    CHECK_THROWS_AS(rig.run("track.setMidiIn", {{"track", device}, {"sourceTrack", other}}), AppError);
    CHECK_THROWS_AS(rig.run("track.setMidiOut", {{"track", keys}, {"sourceTrack", device}}), AppError);
}

TEST_CASE("a project saves a track's device input and loads it back") {
    SourceRig rig;
    const std::string device = rig.addDevice();
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    rig.run("track.setMidiIn", {{"track", keys}, {"sourceTrack", device}, {"channel", 2}});
    rig.controller->save();

    SourceRig other;
    other.controller->open(rig.controller->folder());
    const model::MidiRoute& midiIn = other.controller->project().track(model::TrackId{keys})->midiIn;
    CHECK(midiIn.sourceTrack.value == device);
    CHECK(midiIn.channel == 2);
}

TEST_CASE("recording a track on one channel of a device records that channel of what the device plays") {
    SourceRig rig;
    const std::string device = rig.addDevice();
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    rig.run("track.setMidiIn", {{"track", keys}, {"sourceTrack", device}, {"channel", 2}});
    rig.run("track.setArm", {{"track", keys}, {"value", true}});
    rig.run("transport.record");
    rig.audio->process(48000);
    rig.run("transport.stop");
    rig.audio->process(kBlock);
    REQUIRE(rig.waitForTakes(1) == 1);

    const Json clip = projectState(*rig.controller).at("tracks").at(1).at("clips").at(0);
    const Json notes = rig.run("project.clipEvents", {{"clip", clip.at("id")}}).at("notes");
    REQUIRE(notes.size() == 1);
    CHECK(notes.at(0).at("pitch") == 62);
    CHECK(notes.at(0).at("velocity") == 90);
    CHECK(notes.at(0).at("start") == 960);
}

TEST_CASE("removing a device clears the inputs that take its MIDI") {
    SourceRig rig;
    const std::string device = rig.addDevice();
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    rig.run("track.setMidiIn", {{"track", keys}, {"sourceTrack", device}});
    rig.run("track.remove", {{"track", device}});
    CHECK(rig.controller->project().track(model::TrackId{keys})->midiIn.sourceTrack.empty());
}
