#include <QSettings>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "emerick/app/track_list_model.hpp"
#include "emerick/model/json.hpp"
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

Json route(const ProjectRig& rig, const std::string& trackId, model::AudioRoute model::Track::* field) {
    const model::AudioRoute& route = rig.controller->project().track(model::TrackId{trackId})->*field;
    return {{"device", route.deviceId}, {"channels", route.channels}};
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

TEST_CASE("a track on a device that cannot run at the engine's rate leaves the other outputs changeable") {
    ProjectRig rig;
    model::Document document;
    model::Track speakers;
    speakers.kind = "audio";
    speakers.audioOut = {"interface", {1, 2}};
    model::Track headphones;
    headphones.kind = "audio";
    headphones.audioOut = {"keyboard", {1, 2}};
    document.tracks = {speakers, headphones};
    const auto folder = rig.path("song.erk");
    model::saveProject(document, folder);
    rig.run("project.open", {{"path", folder.string()}});

    rig.run("track.setAudioOut", {{"track", speakers.id.value}, {"device", kMotuId}});
    const Json state = projectState(*rig.controller);
    rig.run("transport.play");

    CHECK_FALSE(state.contains("sessionError"));
    REQUIRE(state.at("routeErrors").size() == 1);
    CHECK(state.at("routeErrors").at(0) ==
          Json{{"track", headphones.id.value},
               {"message", "Digital Keyboard runs at 44100 Hz; set Audio > Sample rate to 44100 and restart Emerick"}});
    CHECK(rig.audio->lastRequest().deviceIds == std::vector<std::string>{kMotuId});
    CHECK(rig.controller->trackErrors() ==
          QVariantMap{{QString::fromStdString(headphones.id.value),
                       "Digital Keyboard runs at 44100 Hz; set Audio > Sample rate to 44100 and restart Emerick"}});
}

TEST_CASE("the stream opens on play with only the outputs, and closes again once stopped") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    rig.controller->flush();
    CHECK_FALSE(rig.audio->isOpen());

    rig.run("transport.play");
    CHECK(rig.audio->lastRequest().deviceIds == std::vector<std::string>{"interface"});
    CHECK(rig.audio->lastRequest().inputDeviceIds.empty());
    rig.run("transport.stop");
    CHECK(processUntil([&] { return !rig.audio->isOpen(); }));

    rig.run("track.setArm", {{"track", vox}, {"value", true}});
    rig.controller->flush();
    CHECK(rig.audio->isOpen());
    CHECK(rig.audio->lastRequest().inputDeviceIds == std::vector<std::string>{"interface"});
    processUntil([] { return false; }, 100);
    CHECK(rig.audio->isOpen());
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

TEST_CASE("the default audio routes start on the first device that fits") {
    const ProjectRig rig;
    CHECK(rig.registry.settingValue("audio.defaultInput") == Json{{"device", "interface"}, {"channels", {1, 2}}});
    CHECK(rig.registry.settingValue("audio.defaultOutput") == Json{{"device", "interface"}, {"channels", {1, 2}}});
}

TEST_CASE("new audio tracks take the default input and output") {
    ProjectRig rig;
    CHECK(rig.registry.setSetting("audio.defaultInput", {{"device", kMotuId}}) ==
          Json{{"device", kMotuId}, {"channels", {1, 2}}});
    rig.registry.setSetting("audio.defaultOutput", {{"device", kMotuId}, {"channels", {3, 4}}});
    rig.registry.setSetting("audio.defaultOutput", {{"channels", {3}}});

    const Json track = rig.run("track.addAudio");

    CHECK(track.at("audioIn").at("device") == kMotuId);
    CHECK(track.at("audioIn").at("channels") == Json{1, 2});
    CHECK(track.at("audioOut").at("device") == kMotuId);
    CHECK(track.at("audioOut").at("channels") == Json{3});
}

TEST_CASE("a default route the device cannot play is refused") {
    ProjectRig rig;
    CHECK_THROWS_AS(rig.registry.setSetting("audio.defaultOutput", {{"device", "interface"}, {"channels", {3}}}),
                    AppError);
    CHECK_THROWS_AS(rig.registry.setSetting("audio.defaultInput", {{"device", "keyboard"}}), AppError);
    CHECK(rig.registry.settingValue("audio.defaultInput").at("device") == "interface");
}

TEST_CASE("a default input whose device is gone gives new tracks the first device that fits") {
    ProjectRig rig;
    QSettings(rig.dir.filePath("settings.ini"), QSettings::IniFormat)
        .setValue("audio.defaultInput", R"({"device":"gone","channels":[1]})");
    const Json track = rig.run("track.addAudio");
    CHECK(track.at("audioIn").at("device") == "interface");
}

TEST_CASE("setting the input of all audio tracks is one undo step that skips other tracks") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const std::string guitar = rig.run("track.addAudio").at("id");
    const std::string keys = rig.run("track.addMidi").at("id");

    const Json result = rig.run("track.setAllAudioIn", {{"device", kMotuId}, {"channels", {2}}});

    CHECK(result.at("tracks") == Json{vox, guitar});
    for (const std::string& id : {vox, guitar}) {
        CHECK(route(rig, id, &model::Track::audioIn) == Json{{"device", kMotuId}, {"channels", {2}}});
    }
    CHECK(route(rig, keys, &model::Track::audioIn).at("device") == "");
    rig.run("project.undo");
    for (const std::string& id : {vox, guitar}) {
        CHECK(route(rig, id, &model::Track::audioIn).at("device") == "interface");
    }
}

TEST_CASE("setting the output of all audio tracks without params applies the default output") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    rig.registry.setSetting("audio.defaultOutput", {{"device", kMotuId}, {"channels", {3, 4}}});

    rig.run("track.setAllAudioOut");

    CHECK(route(rig, vox, &model::Track::audioOut) == Json{{"device", kMotuId}, {"channels", {3, 4}}});
}
