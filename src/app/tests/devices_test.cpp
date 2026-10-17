#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <vector>

#include "emerick/app/track_list_model.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

constexpr std::chrono::milliseconds kSlowListing{50};

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
    ProjectRig rig({kMidiPort}, kSlowListing);
    const InspectorBindings inspector(*rig.controller);
    REQUIRE(inspector.object);
    const std::string vox = rig.runAsAgent("track.addAudio").at("result").at("id");
    const std::string keys = rig.runAsAgent("track.addMidi").at("result").at("id");
    const int audioListings = rig.audioLister->listings;
    const int midiListings = rig.midiLister->listings;

    const auto start = std::chrono::steady_clock::now();
    rig.runAsAgent("track.setAudioIn", {{"track", vox}, {"channels", {2}}});
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    rig.runAsAgent("track.setMidiOut", {{"track", keys}, {"port", kMidiPort}});

    CHECK(rig.audioLister->listings == audioListings);
    CHECK(rig.midiLister->listings == midiListings);
    CAPTURE(elapsed.count());
    CHECK(elapsed < kSlowListing);
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
    CHECK(devices.at("audio").at(0).at("id") == "interface");
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
