#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "emerick/app/track_list_model.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

std::vector<std::string> rowIds(const TrackListModel& model) {
    std::vector<std::string> ids;
    for (int row = 0; row < model.rowCount(); ++row) {
        ids.push_back(
            model.data(model.index(row), static_cast<int>(TrackListModel::Role::id)).toString().toStdString());
    }
    return ids;
}

std::vector<std::string> documentIds(const ProjectRig& rig) {
    std::vector<std::string> ids;
    for (const model::Track& track : rig.controller->project().document().tracks) {
        ids.push_back(track.id.value);
    }
    return ids;
}

}  // namespace

TEST_CASE("changing a track's route lists no devices") {
    ProjectRig rig;
    const InspectorBindings inspector(*rig.controller);
    REQUIRE(inspector.object);
    const std::string vox = rig.runAsAgent("track.addAudio").at("result").at("id");
    const std::string keys = rig.runAsAgent("track.addMidi").at("result").at("id");
    int pickerUpdates = 0;
    QObject::connect(rig.controller.get(), &ProjectController::devicesChanged, [&] { ++pickerUpdates; });
    const int audioListings = rig.audioLister->listings;
    const int midiListings = rig.midiLister->listings;

    rig.runAsAgent("track.setAudioIn", {{"track", vox}, {"channels", {2}}});
    rig.runAsAgent("track.setMidiOut", {{"track", keys}, {"port", kMidiPort}});

    CHECK(rig.audioLister->listings == audioListings);
    CHECK(rig.midiLister->listings == midiListings);
    CHECK(pickerUpdates == 0);
}

TEST_CASE("devices.refresh lists the devices again and tells the route pickers") {
    ProjectRig rig;
    int changes = 0;
    QObject::connect(rig.controller.get(), &ProjectController::devicesChanged, [&] { ++changes; });
    const int audioListings = rig.audioLister->listings;
    const int midiListings = rig.midiLister->listings;
    const Json devices = rig.run("devices.refresh");
    CHECK(rig.audioLister->listings == audioListings + 1);
    CHECK(rig.midiLister->listings == midiListings + 1);
    CHECK(changes == 1);
    CHECK(devices.at("audio").at(1).at("sampleRates") == Json::array({44100.0}));
}

TEST_CASE("a device change reported on another thread lists the devices again on the UI thread") {
    ProjectRig rig;
    int changes = 0;
    QObject::connect(rig.controller.get(), &ProjectController::devicesChanged, [&] { ++changes; });
    const int listings = rig.audioLister->listings;
    rig.audioLister->plug();
    REQUIRE(processUntil([&] { return changes == 1; }));
    CHECK(rig.audioLister->listings == listings + 1);
    processUntil([] { return false; }, 100);
    CHECK(changes == 1);
}

TEST_CASE("a route to a device that cannot run at the engine's rate fails with the rate to set") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    rig.controller->flush();
    const int opens = rig.audioLister->opens;
    try {
        rig.run("track.setAudioIn", {{"track", vox}, {"device", "keyboard"}, {"channels", {1, 2}}});
        FAIL("the route action accepted a 44100 Hz device in a 48000 Hz engine");
    } catch (const AppError& error) {
        CHECK(std::string{error.what()} ==
              "Digital Keyboard runs at 44100 Hz; set Audio > Sample rate to 44100 and restart Emerick");
    }
    rig.controller->flush();
    CHECK(rig.controller->project().track(model::TrackId{vox})->audioIn.deviceId == "interface");
    CHECK(rig.audioLister->opens == opens);
}

TEST_CASE("one group of edits publishes the session once and inserts its rows without a reset") {
    ProjectRig rig;
    TrackListModel model{*rig.controller, rig.registry};
    int published = 0;
    int resets = 0;
    int inserts = 0;
    QObject::connect(rig.controller.get(), &ProjectController::projectChanged, [&] { ++published; });
    QObject::connect(&model, &TrackListModel::modelReset, [&] { ++resets; });
    QObject::connect(&model, &TrackListModel::rowsInserted, [&] { ++inserts; });

    rig.controller->project().group("add parts", [&] {
        for (const char* name : {"Bass", "Drums", "Keys"}) {
            model::Track track;
            track.name = name;
            rig.controller->project().addTrack(std::move(track));
        }
    });
    QCoreApplication::processEvents();

    CHECK(published == 1);
    CHECK(inserts == 1);
    CHECK(resets == 0);
    CHECK(rowIds(model) == documentIds(rig));
}

TEST_CASE("the track list moves, removes and restores rows to follow the document") {
    ProjectRig rig;
    TrackListModel model{*rig.controller, rig.registry};
    int resets = 0;
    QObject::connect(&model, &TrackListModel::modelReset, [&] { ++resets; });
    for (const char* name : {"A", "B", "C", "D"}) {
        rig.run("track.addMidi", {{"name", name}});
    }
    rig.run("track.move", {{"track", "D"}, {"index", 0}});
    CHECK(rowIds(model) == documentIds(rig));
    rig.run("track.remove", {{"track", "B"}});
    CHECK(rowIds(model) == documentIds(rig));
    rig.run("project.undo");
    rig.run("project.undo");
    CHECK(rowIds(model) == documentIds(rig));
    rig.run("track.rename", {{"track", "C"}, {"name", "Cello"}});
    CHECK(model.data(model.index(2), static_cast<int>(TrackListModel::Role::name)).toString() == "Cello");
    CHECK(resets == 0);
}
