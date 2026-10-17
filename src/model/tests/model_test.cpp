#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "emerick/model/json.hpp"
#include "emerick/model/project.hpp"

using namespace emerick::model;

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
    track.audioIn = {"genos", {1, 2}};
    track.midiIn = {"Digital Keyboard Port 1", 3, true, true};
    track.data = {{"voices", {1, 2, 3}}};
    track.clips.emplace_back(AudioClip{ClipId::make(), 960, 3840, 44100, "audio/take1.wav", -3.5, 10, 20});
    track.clips.emplace_back(MidiClip{ClipId::make(), 0, 1920, {{0, {0x90, 60, 100}}, {480, {0xF0, 0x43, 0xF7}}}});
    document.tracks.push_back(track);
    document.tracks.push_back(named("Bass"));
    document.clockPorts = {"Digital Keyboard Port 1"};
    return document;
}

}  // namespace

TEST_CASE("document round trips through json") {
    const Document document = sampleDocument();
    const Document loaded = documentFromJson(toJson(document));
    CHECK(toJson(loaded) == toJson(document));
    CHECK(loaded.tempo.bpm == 96.0);
    CHECK(loaded.tracks[0].id == document.tracks[0].id);
    CHECK(std::get<MidiClip>(loaded.tracks[0].clips[1]).events[1].bytes == std::vector<std::uint8_t>{0xF0, 0x43, 0xF7});
    CHECK(loaded.tracks[0].midiIn.sysex);
    CHECK(loaded.tracks[0].data == document.tracks[0].data);
    CHECK(toJson(document)["format"] == 1);
}

TEST_CASE("unknown format is rejected") {
    auto json = toJson(Document{});
    json["format"] = 2;
    CHECK_THROWS_AS(documentFromJson(json), std::runtime_error);
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
