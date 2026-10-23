#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "emerick/model/json.hpp"
#include "emerick/model/project.hpp"

using namespace emerick::model;
using emerick::core::FadeShape;

namespace {

Track named(const std::string& name) {
    Track track;
    track.name = name;
    return track;
}

std::vector<std::string> names(const Project& project) {
    std::vector<std::string> result;
    for (const Track& track : project.document().tracks) {
        result.push_back(track.name);
    }
    return result;
}

Document sampleDocument() {
    Document document;
    document.tempo = {96.0, {3, 4}};
    Track track = named("Right 1");
    track.kind = "yamaha.chord";
    track.icon = "piano";
    track.color = 7;
    track.armed = true;
    track.gainDb = -6.0;
    track.pan = -0.25;
    track.audioIn = {"genos", {1, 2}};
    track.midiIn = {"Digital Keyboard Port 1", 3, true, true};
    track.data = {{"voices", {1, 2, 3}}};
    track.clips.emplace_back(AudioClip{ClipId::make(), 960, 3840, 44100, "audio/take1.wav", -3.5, 10, 20,
                                       FadeShape::sCurve, FadeShape::equalPower, 0.25, -0.5});
    track.clips.emplace_back(MidiClip{ClipId::make(),
                                      0,
                                      1920,
                                      {{480, {0xF0, 0x43, 0xF7}}},
                                      {{.pitch = 60, .velocity = 90, .start = 0, .length = 480, .channel = 3}}});
    document.tracks.push_back(track);
    document.tracks.push_back(named("Bass"));
    document.markers = {{.name = "Intro", .at = 0}, {.name = "Chorus", .at = 7680, .color = 3}};
    document.clockPorts = {"Digital Keyboard Port 1"};
    document.pluginData = {{"yamaha", {{"voices", {{{"channel", 9}}}}}}};
    return document;
}

}  // namespace

TEST_CASE("document round trips through json") {
    const Document document = sampleDocument();
    const Document loaded = documentFromJson(toJson(document));
    CHECK(toJson(loaded) == toJson(document));
    CHECK(loaded.tempo.bpm == 96.0);
    CHECK(loaded.tracks[0].id == document.tracks[0].id);
    CHECK(std::get<MidiClip>(loaded.tracks[0].clips[1]).events[0].bytes == std::vector<std::uint8_t>{0xF0, 0x43, 0xF7});
    CHECK(std::get<MidiClip>(loaded.tracks[0].clips[1]).notes == std::get<MidiClip>(document.tracks[0].clips[1]).notes);
    CHECK(loaded.tracks[0].midiIn.sysex);
    CHECK(loaded.tracks[0].data == document.tracks[0].data);
    CHECK(toJson(document)["format"] == kFormatVersion);
    CHECK(loaded.markers == document.markers);
}

TEST_CASE("sends and output buses round trip, and a format 3 project loads with none") {
    Document document = sampleDocument();
    Track bus = named("Reverb");
    bus.kind = "bus";
    document.tracks[0].sends = {{.bus = bus.id, .levelDb = -6.0, .pre = true, .enabled = false}};
    document.tracks[1].outputBus = bus.id;
    document.tracks.push_back(bus);
    const Document loaded = documentFromJson(toJson(document));
    CHECK(loaded.tracks[0].sends == document.tracks[0].sends);
    CHECK(loaded.tracks[1].outputBus == bus.id);

    auto json = toJson(document);
    json["format"] = 3;
    for (auto& track : json["tracks"]) {
        track.erase("sends");
        track.erase("outputBus");
    }
    for (const Track& track : documentFromJson(json).tracks) {
        CHECK(track.sends.empty());
        CHECK(track.outputBus.empty());
    }
}

TEST_CASE("inserts round trip in order, and a format 6 project loads with none") {
    Document document = sampleDocument();
    document.tracks[1].inserts = {{.type = "eq", .bypass = false, .params = {{"peak2Gain", 3.5}}},
                                  {.type = "reverb", .bypass = true, .params = {}}};
    const Document loaded = documentFromJson(toJson(document));
    CHECK(loaded.tracks[1].inserts == document.tracks[1].inserts);
    CHECK(trackWithInsert(loaded, document.tracks[1].inserts[1].id) == &loaded.tracks[1]);

    auto json = toJson(document);
    json["format"] = 6;
    for (auto& track : json["tracks"]) {
        track.erase("inserts");
    }
    for (const Track& track : documentFromJson(json).tracks) {
        CHECK(track.inserts.empty());
    }
}

TEST_CASE("reaches follows sends and output buses through a chain of buses") {
    Document document;
    Track vox = named("Vox");
    Track verb = named("Verb");
    Track master = named("Master");
    vox.sends = {{.bus = verb.id, .levelDb = 0.0, .pre = false, .enabled = false}};
    verb.outputBus = master.id;
    document.tracks = {vox, verb, master};
    CHECK(reaches(document, vox.id, master.id));
    CHECK(reaches(document, verb.id, master.id));
    CHECK_FALSE(reaches(document, master.id, vox.id));
    CHECK_FALSE(reaches(document, master.id, verb.id));
}

TEST_CASE("unknown format is rejected") {
    auto json = toJson(Document{});
    json["format"] = kFormatVersion + 1;
    CHECK_THROWS_AS(documentFromJson(json), std::runtime_error);
}

TEST_CASE("a format 1 project loads with no markers") {
    auto json = toJson(sampleDocument());
    json["format"] = 1;
    json.erase("markers");
    const Document loaded = documentFromJson(json);
    CHECK(loaded.markers.empty());
    CHECK(loaded.tracks.size() == 2);
}

TEST_CASE("a format 2 project loads with every track centered and saves its pan") {
    auto json = toJson(sampleDocument());
    json["format"] = 2;
    for (auto& track : json["tracks"]) {
        track.erase("pan");
    }
    for (const Track& track : documentFromJson(json).tracks) {
        CHECK(track.pan == 0.0);
    }
    CHECK(documentFromJson(toJson(sampleDocument())).tracks[0].pan == -0.25);
}

TEST_CASE("fade shapes and curves round trip, and a format 2 project keeps linear fades") {
    const Document document = sampleDocument();
    const auto& saved = std::get<AudioClip>(document.tracks[0].clips[0]);
    CHECK(std::get<AudioClip>(documentFromJson(toJson(document)).tracks[0].clips[0]) == saved);

    auto json = toJson(document);
    json["format"] = 2;
    for (const char* key : {"fadeInShape", "fadeOutShape", "fadeInCurve", "fadeOutCurve"}) {
        json["tracks"][0]["clips"][0].erase(key);
    }
    const auto old = std::get<AudioClip>(documentFromJson(json).tracks[0].clips[0]);
    CHECK(old.fadeIn == 10);
    CHECK(old.fadeInShape == FadeShape::linear);
    CHECK(old.fadeOutShape == FadeShape::linear);
    CHECK(old.fadeInCurve == 0.0);
    CHECK(old.fadeOutCurve == 0.0);

    json["tracks"][0]["clips"][0]["fadeInShape"] = "wobbly";
    CHECK_THROWS_AS(documentFromJson(json), std::runtime_error);
}

TEST_CASE("markers stay sorted by position and undo and redo as one step each") {
    Project project;
    const Marker verse{.name = "Verse", .at = 3840};
    const Marker intro{.name = "Intro", .at = 0, .color = 2};
    project.setMarkers({verse, intro}, "add markers");
    CHECK(project.document().markers == std::vector{intro, verse});
    CHECK(findMarker(project.document(), verse.id)->name == "Verse");

    Marker moved = verse;
    moved.at = 0;
    project.setMarkers({intro, moved}, "move marker");
    CHECK(project.document().markers.front().at == 0);
    project.undo();
    CHECK(project.document().markers == std::vector{intro, verse});
    project.undo();
    CHECK(project.document().markers.empty());
    project.redo();
    CHECK(project.undoName() == "add markers");
}

TEST_CASE("project saves to and loads from a folder") {
    const auto folder = std::filesystem::temp_directory_path() / "emerick_model_test_project";
    std::filesystem::remove_all(folder);
    const Document document = sampleDocument();
    saveProject(document, folder);
    CHECK(std::filesystem::is_directory(folder / "audio"));
    saveProject(document, folder);
    CHECK(toJson(loadProject(folder)) == toJson(document));
    CHECK_FALSE(std::filesystem::exists(folder / "project.json.tmp"));
    std::filesystem::remove_all(folder);
    CHECK_THROWS_AS(loadProject(folder), std::runtime_error);
}

TEST_CASE("add and remove track undo and redo") {
    Project project;
    const Track first = named("a");
    const Track second = named("b");
    project.addTrack(first);
    project.addTrack(second, 0);
    CHECK(names(project) == std::vector<std::string>{"b", "a"});

    CHECK(project.undoName() == "add track b");
    project.undo();
    CHECK(names(project) == std::vector<std::string>{"a"});
    CHECK(project.redoName() == "add track b");
    project.redo();
    CHECK(names(project) == std::vector<std::string>{"b", "a"});

    CHECK(project.removeTrack(first.id));
    CHECK(names(project) == std::vector<std::string>{"b"});
    project.undo();
    CHECK(names(project) == std::vector<std::string>{"b", "a"});
    project.redo();
    CHECK(names(project) == std::vector<std::string>{"b"});
    CHECK_FALSE(project.removeTrack(first.id));
}

TEST_CASE("edit track undoes as one step and keeps the id") {
    Project project;
    const Track track = named("old");
    project.addTrack(track);
    CHECK(project.editTrack(track.id, "rename track", [](Track& edited) {
        edited.name = "new";
        edited.mute = true;
    }));
    CHECK(project.track(track.id)->name == "new");
    CHECK(project.undoName() == "rename track");
    project.undo();
    CHECK(project.track(track.id)->name == "old");
    CHECK_FALSE(project.track(track.id)->mute);
    project.redo();
    CHECK(project.track(track.id)->mute);
    CHECK_FALSE(project.editTrack(TrackId::make(), "x", [](Track&) {}));
}

TEST_CASE("move track undo and redo") {
    Project project;
    const Track a = named("a");
    project.addTrack(a);
    project.addTrack(named("b"));
    project.addTrack(named("c"));
    CHECK(project.moveTrack(a.id, 2));
    CHECK(names(project) == std::vector<std::string>{"b", "c", "a"});
    project.undo();
    CHECK(names(project) == std::vector<std::string>{"a", "b", "c"});
    project.redo();
    CHECK(names(project) == std::vector<std::string>{"b", "c", "a"});
    CHECK(project.moveTrack(a.id, 0));
    CHECK(names(project) == std::vector<std::string>{"a", "b", "c"});
    project.undo();
    CHECK(names(project) == std::vector<std::string>{"b", "c", "a"});
    CHECK_FALSE(project.moveTrack(a.id, 3));
}

TEST_CASE("set tempo undo and redo") {
    Project project;
    project.setTempo({140.0, {6, 8}});
    CHECK(project.document().tempo.bpm == 140.0);
    project.undo();
    CHECK(project.document().tempo.bpm == 120.0);
    project.redo();
    CHECK(project.document().tempo.meter.numerator == 6);
}

TEST_CASE("set clock ports undo and redo") {
    Project project;
    project.setClockPorts({"Port 1"});
    CHECK(project.document().clockPorts == std::vector<std::string>{"Port 1"});
    project.undo();
    CHECK(project.document().clockPorts.empty());
    project.redo();
    CHECK(project.document().clockPorts.size() == 1);
}

TEST_CASE("plugin data sets, clears, undoes and redoes") {
    Project project;
    project.setPluginData("yamaha", {{"voices", 1}}, "load voices");
    CHECK(project.document().pluginData.at("yamaha").at("voices") == 1);
    project.setPluginData("yamaha", nullptr, "clear voices");
    CHECK_FALSE(project.document().pluginData.contains("yamaha"));
    project.undo();
    CHECK(project.document().pluginData.at("yamaha").at("voices") == 1);
    project.undo();
    CHECK(project.document().pluginData.empty());
    project.redo();
    CHECK(project.undoName() == "load voices");
}

TEST_CASE("a group of edits undoes and redoes as one step") {
    Project project;
    project.addTrack(named("before"));
    project.group("set up", [&] {
        project.addTrack(named("a"));
        project.addTrack(named("b"));
        project.setClockPorts({"Port 1"});
    });
    CHECK(names(project) == std::vector<std::string>{"before", "a", "b"});
    CHECK(project.undoName() == "set up");
    project.undo();
    CHECK(names(project) == std::vector<std::string>{"before"});
    CHECK(project.document().clockPorts.empty());
    project.redo();
    CHECK(names(project) == std::vector<std::string>{"before", "a", "b"});
    CHECK(project.document().clockPorts.size() == 1);
    project.undo();
    project.undo();
    CHECK(project.document().tracks.empty());
}

TEST_CASE("edits that throw leave the document, the history and the redo steps as they were") {
    Project project;
    project.addTrack(named("kept"));
    project.addTrack(named("undone"));
    project.undo();
    const Document before = project.document();
    int changes = 0;
    project.addListener([&](const Change&) { ++changes; });
    const auto editThenThrow = [&] {
        project.addTrack(named("half"));
        project.editTrack(project.document().tracks.front().id, "rename", [](Track& track) { track.name = "x"; });
        throw std::runtime_error("failed halfway");
    };

    CHECK_THROWS_AS(project.atomic(editThenThrow), std::runtime_error);
    CHECK(project.document() == before);
    CHECK(project.undoName() == "add track kept");
    CHECK(project.redoName() == "add track undone");
    CHECK(changes == 3);

    CHECK_THROWS_AS(project.group("set up", editThenThrow), std::runtime_error);
    CHECK(project.document() == before);
    CHECK(project.redoName() == "add track undone");
    CHECK(project.redo());
    CHECK(names(project) == std::vector<std::string>{"kept", "undone"});
}

TEST_CASE("an edit that changes nothing leaves no undo step") {
    Project project;
    project.addTrack(named("a"));
    const TrackId id = project.document().tracks.front().id;
    CHECK(project.editTrack(id, "rename", [](Track& track) { track.name = "a"; }));
    CHECK(project.moveTrack(id, 0));
    project.setTempo(project.document().tempo);
    project.setClockPorts({});
    project.setPluginData("plugin", nullptr, "clear");
    project.setMarkers(project.document().markers, "move marker");
    CHECK(project.undoName() == "add track a");
}

TEST_CASE("a new edit clears the redo stack") {
    Project project;
    project.addTrack(named("a"));
    project.undo();
    project.addTrack(named("b"));
    CHECK_FALSE(project.redo());
    CHECK_FALSE(project.redoName());
    CHECK(project.undo());
    CHECK_FALSE(project.undo());
    CHECK_FALSE(project.undoName());
}

TEST_CASE("listeners hear track and document changes until removed") {
    Project project;
    std::vector<Change> changes;
    const auto handle = project.addListener([&](const Change& change) { changes.push_back(change); });

    const Track track = named("a");
    project.addTrack(track);
    project.editTrack(track.id, "edit", [](Track& edited) { edited.solo = true; });
    project.undo();
    project.setTempo({100.0, {}});

    REQUIRE(changes.size() == 4);
    CHECK_FALSE(changes[0].track);
    CHECK(changes[1].track == track.id);
    CHECK(changes[2].track == track.id);
    CHECK_FALSE(changes[3].track);

    project.removeListener(handle);
    project.undo();
    CHECK(changes.size() == 4);
}

TEST_CASE("lookups find tracks and clips by id") {
    Project project(sampleDocument());
    const Track& track = project.document().tracks[0];
    const ClipId audioId = clipId(track.clips[0]);

    REQUIRE(project.track(track.id));
    CHECK(project.track(track.id)->name == "Right 1");
    CHECK_FALSE(project.track(TrackId::make()));

    const auto found = project.findClip(audioId);
    REQUIRE(found);
    CHECK(found->track->id == track.id);
    CHECK(std::holds_alternative<AudioClip>(*found->clip));
    CHECK_FALSE(project.findClip(ClipId::make()));
}

TEST_CASE("overlapping clips stack and survive edits, undo and json") {
    Project project;
    const Track track = named("takes");
    project.addTrack(track);
    const AudioClip take1{ClipId::make(), 0, 3840, 0, "audio/a.wav"};
    const AudioClip take2{ClipId::make(), 960, 3840, 0, "audio/b.wav"};
    project.editTrack(track.id, "add clip", [&](Track& edited) { edited.clips.emplace_back(take1); });
    project.editTrack(track.id, "add clip", [&](Track& edited) { edited.clips.emplace_back(take2); });

    CHECK(project.track(track.id)->clips.size() == 2);
    CHECK(project.findClip(take1.id));
    CHECK(project.findClip(take2.id));

    const Document loaded = documentFromJson(toJson(project.document()));
    CHECK(loaded.tracks[0].clips.size() == 2);

    project.undo();
    CHECK(project.track(track.id)->clips.size() == 1);
    CHECK(project.findClip(take1.id));
    CHECK_FALSE(project.findClip(take2.id));
}

TEST_CASE("reset replaces the document and clears history") {
    Project project;
    project.addTrack(named("a"));
    std::size_t calls = 0;
    project.addListener([&](const Change&) { ++calls; });
    project.reset(sampleDocument());
    CHECK(project.document().tracks.size() == 2);
    CHECK_FALSE(project.undoName());
    CHECK(calls == 1);
}

namespace {

void setGain(Project& project, const TrackId& id, double db) {
    project.editTrack(id, "set track gain", [&](Track& edited) { edited.gainDb = db; });
}

}  // namespace

TEST_CASE("a gesture of many updates follows each one and undoes as one step") {
    Project project;
    const Track track = named("vox");
    project.addTrack(track);
    std::vector<Change> heard;
    project.addListener([&](const Change& change) { heard.push_back(change); });

    for (const double db : {-1.0, -2.0, -3.0}) {
        project.updateGesture("drag gain", [&] { setGain(project, track.id, db); });
        CHECK(project.track(track.id)->gainDb == db);
    }
    CHECK(heard.size() == 3);
    CHECK(heard.back().track == track.id);
    CHECK(project.endGesture());
    CHECK(project.undoName() == "set track gain");

    project.undo();
    CHECK(project.track(track.id)->gainDb == 0.0);
    CHECK(project.undoName() == "add track vox");
}

TEST_CASE("a gesture of several edits per update undoes as one step named after the gesture") {
    Project project;
    const Track first = named("a");
    const Track second = named("b");
    project.addTrack(first);
    project.addTrack(second);
    for (const double db : {1.0, 2.0}) {
        project.updateGesture("drag gains", [&] {
            setGain(project, first.id, db);
            setGain(project, second.id, db);
        });
    }
    CHECK(project.endGesture());
    CHECK(project.undoName() == "drag gains");
    project.undo();
    CHECK(project.track(first.id)->gainDb == 0.0);
    CHECK(project.track(second.id)->gainDb == 0.0);
}

TEST_CASE("cancelling a gesture reverts its edits and leaves no undo step") {
    Project project;
    const Track track = named("vox");
    project.addTrack(track);
    project.updateGesture("drag gain", [&] { setGain(project, track.id, -6.0); });
    project.cancelGesture();
    CHECK(project.track(track.id)->gainDb == 0.0);
    CHECK(project.undoName() == "add track vox");
    CHECK_FALSE(project.endGesture());
}

TEST_CASE("a gesture that returns to where it started leaves no undo step") {
    Project project;
    const Track track = named("vox");
    project.addTrack(track);
    project.updateGesture("drag gain", [&] { setGain(project, track.id, -6.0); });
    project.updateGesture("drag gain", [&] { setGain(project, track.id, 0.0); });
    CHECK_FALSE(project.endGesture());
    CHECK(project.undoName() == "add track vox");
    CHECK_FALSE(project.redoName());
}

TEST_CASE("an edit during a gesture keeps the gesture's step and later updates start a new one") {
    Project project;
    const Track track = named("vox");
    project.addTrack(track);
    project.updateGesture("drag gain", [&] { setGain(project, track.id, -6.0); });
    project.addTrack(named("take"));
    project.updateGesture("drag gain", [&] { setGain(project, track.id, -9.0); });
    CHECK(project.endGesture());
    CHECK(names(project) == std::vector<std::string>{"vox", "take"});
    project.undo();
    CHECK(project.track(track.id)->gainDb == -6.0);
    CHECK(names(project) == std::vector<std::string>{"vox", "take"});
}
