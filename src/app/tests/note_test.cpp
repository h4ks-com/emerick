#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "emerick/app/session_mapping.hpp"
#include "emerick/model/json.hpp"
#include "midi_file.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

struct NoteRig {
    ProjectRig rig;
    std::string track = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    std::string clip = rig.run("midi.addClip", {{"track", track}, {"at", 1920}}).at("id");

    Json add(int pitch, core::Ticks start, core::Ticks length = 240, int velocity = 100) const {
        return rig
            .run("midi.addNote",
                 {{"clip", clip}, {"pitch", pitch}, {"start", start}, {"length", length}, {"velocity", velocity}})
            .at("notes")
            .at(0);
    }

    [[nodiscard]] const model::MidiClip& midi() const {
        return std::get<model::MidiClip>(*rig.controller->project().findClip(model::ClipId{clip})->clip);
    }

    [[nodiscard]] const model::Note& note(const Json& added) const {
        return *std::ranges::find(midi().notes, model::NoteId{added.at("id")}, &model::Note::id);
    }

    Json run(const std::string& id, Json params) const {
        params["clip"] = clip;
        return rig.run(id, params);
    }
};

}  // namespace

TEST_CASE("each note action edits the clip by note id as one undo step") {
    NoteRig keys;
    const Json c = keys.add(60, 0);
    const Json e = keys.add(64, 480);
    CHECK(keys.midi().notes.size() == 2);
    CHECK(keys.rig.controller->project().undoName() == "add note");

    keys.run("midi.moveNotes", {{"notes", {c.at("id")}}, {"by", 240}, {"pitch", 2}});
    CHECK((keys.note(c).start == 240 && keys.note(c).pitch == 62 && keys.note(e).start == 480));
    keys.run("midi.resizeNotes", {{"notes", {c.at("id"), e.at("id")}}, {"by", 120}});
    CHECK((keys.note(c).length == 360 && keys.note(e).length == 360));
    keys.run("midi.setVelocity", {{"notes", {e.at("id")}}, {"velocity", 30}});
    CHECK(keys.note(e).velocity == 30);
    keys.run("midi.deleteNotes", {{"notes", {c.at("id")}}});
    CHECK(keys.midi().notes.size() == 1);

    for (const char* step : {"delete notes", "set note velocity", "resize notes", "move notes"}) {
        CHECK(keys.rig.controller->project().undoName() == step);
        keys.rig.run("project.undo");
    }
    CHECK((keys.note(c).start == 0 && keys.note(c).pitch == 60 && keys.note(c).length == 240));
    CHECK(keys.note(e).velocity == 100);
}

TEST_CASE("a new note takes the last length and the selection follows the piano roll") {
    NoteRig keys;
    keys.rig.run("midi.editClip", {{"clip", keys.clip}});
    const Json first = keys.add(60, 0, 720);
    CHECK(keys.rig.nav.selectedNotes() == QStringList{qs(first.at("id"))});
    const Json second = keys.rig.run("midi.addNote", {{"pitch", 62}, {"start", 960}}).at("notes").at(0);
    CHECK(second.at("length") == 720);
    keys.rig.run("midi.resizeNotes", {{"length", 100}});
    CHECK(keys.note(second).length == 100);
    CHECK(keys.note(first).length == 720);

    CHECK_THROWS_AS(keys.add(128, 0), AppError);
    CHECK_THROWS_AS(keys.add(60, 3840), AppError);
    CHECK_THROWS_AS(keys.run("midi.moveNotes", {{"notes", {"missing"}}, {"by", 1}}), AppError);
    CHECK(keys.midi().notes.size() == 2);
}

TEST_CASE("moving notes stops at the clip start and the keyboard edges") {
    NoteRig keys;
    const Json low = keys.add(2, 100);
    const Json high = keys.add(120, 500);
    keys.run("midi.moveNotes", {{"notes", {low.at("id"), high.at("id")}}, {"by", -400}, {"pitch", 12}});
    CHECK((keys.note(low).start == 0 && keys.note(high).start == 400));
    CHECK((keys.note(low).pitch == 9 && keys.note(high).pitch == 127));
}

TEST_CASE("quantize moves note starts toward the grid by its strength") {
    NoteRig keys;
    const Json early = keys.add(60, 200);
    const Json late = keys.add(62, 500);
    keys.run("midi.quantize", {{"notes", {early.at("id")}}, {"strength", 0.5}});
    CHECK(keys.note(early).start == 220);
    CHECK(keys.note(late).start == 500);
    keys.run("midi.quantize", {});
    CHECK((keys.note(early).start == 240 && keys.note(late).start == 480));
    keys.rig.registry.setSetting("midi.grid", 4);
    keys.run("midi.quantize", {});
    CHECK((keys.note(early).start == 0 && keys.note(late).start == 960));
    CHECK(keys.note(early).length == 240);
}

TEST_CASE("transpose keeps every pitch from 0 to 127 or refuses the whole edit") {
    NoteRig keys;
    const Json low = keys.add(5, 0);
    const Json high = keys.add(120, 240);
    keys.run("midi.transpose", {{"semitones", 7}});
    CHECK((keys.note(low).pitch == 12 && keys.note(high).pitch == 127));
    CHECK_THROWS_WITH(keys.run("midi.transpose", {{"semitones", 1}}), Catch::Matchers::ContainsSubstring("127"));
    CHECK_THROWS_AS(keys.run("midi.transpose", {{"notes", {low.at("id")}}, {"semitones", -13}}), AppError);
    CHECK((keys.note(low).pitch == 12 && keys.note(high).pitch == 127));
    CHECK(keys.rig.controller->project().undoName() == "transpose notes");
}

TEST_CASE("notes copy and paste within the clip with new ids at the playhead") {
    NoteRig keys;
    keys.rig.run("midi.editClip", {{"clip", keys.clip}});
    keys.add(60, 0);
    keys.add(64, 240);
    keys.rig.run("midi.selectNotes", {{"all", true}});
    keys.rig.run("midi.copyNotes");
    keys.rig.run("transport.locate", {{"position", 1920 + 960}});
    const Json pasted = keys.rig.run("midi.pasteNotes");
    CHECK(pasted.at("notes").size() == 2);
    CHECK(pasted.at("notes").at(0).at("start") == 960);
    CHECK(keys.midi().notes.size() == 4);
    CHECK(keys.rig.nav.selectedNotes().size() == 2);
}

TEST_CASE("an edit to one track converts only that track again and reuses the other tracks") {
    NoteRig keys;
    keys.add(60, 0);
    const std::string other = keys.rig.run("track.addMidi", {{"name", "Bass"}}).at("id");
    const std::string otherClip = keys.rig.run("midi.addClip", {{"track", other}, {"at", 0}}).at("id");
    keys.rig.run("midi.addNote", {{"clip", otherClip}, {"pitch", 40}, {"start", 0}});
    const auto convert = [&](EngineTrackCache& cache) {
        return toSession(keys.rig.controller->project().document(), keys.rig.registry, keys.rig.controller->folder(),
                         cache);
    };
    EngineTrackCache cache;
    const engine::Session before = convert(cache);

    keys.add(64, 480);
    const engine::Session after = convert(cache);

    CHECK(after.tracks.at(0).midiClips.shared() != before.tracks.at(0).midiClips.shared());
    CHECK(after.tracks.at(0).midiClips.at(0).events.size() == 4);
    CHECK(after.tracks.at(1).midiClips.shared() == before.tracks.at(1).midiClips.shared());
}

TEST_CASE("notes play and export as the note-ons and note-offs they came from") {
    NoteRig keys;
    keys.add(60, 0, 480, 90);
    keys.add(67, 960, 960, 70);
    const engine::Session session =
        toSession(keys.rig.controller->project().document(), keys.rig.registry, keys.rig.controller->folder());
    CHECK(session.tracks.at(0).midiClips.at(0).events ==
          std::vector<engine::MidiEvent>{
              {0, {0x90, 60, 90}}, {480, {0x80, 60, 0}}, {960, {0x90, 67, 70}}, {1920, {0x80, 67, 0}}});

    keys.rig.run("export.logic", {{"folder", keys.rig.path("out").string()}});
    const libremidi::reader midi = readMidiFile(keys.rig.path("out") / "Untitled 1 for Logic" / "MIDI.mid");
    std::vector<std::pair<int, libremidi::midi_bytes>> notes;
    for (const auto& event : midi.tracks.at(1)) {
        if (!event.m.is_meta_event()) {
            notes.emplace_back(event.tick, event.m.bytes);
        }
    }
    CHECK(notes == std::vector<std::pair<int, libremidi::midi_bytes>>{
                       {1920, {0x90, 60, 90}}, {2400, {0x80, 60, 0}}, {2880, {0x90, 67, 70}}, {3840, {0x80, 67, 0}}});
}

TEST_CASE("a piano roll key plays on the track's port and on the channel playback sends the clip's notes on") {
    NoteRig keys;
    keys.rig.run("midi.addNote", {{"clip", keys.clip}, {"pitch", 40}, {"start", 0}, {"channel", 11}});
    keys.rig.run("track.setMidiOut", {{"track", keys.track}, {"port", kMidiPort}, {"channel", 0}});
    const Json key = {{"track", keys.track}, {"clip", keys.clip}, {"pitch", 60}};
    CHECK(keys.rig.run("midi.audition", key).at("bytes") == Json{0x9A, 60, 100});
    CHECK(keys.rig.run("midi.audition", {{"track", keys.track}, {"pitch", 60}}).at("bytes").at(0) == 0x90);
    keys.rig.run("track.setMidiOut", {{"track", keys.track}, {"channel", 3}});
    CHECK(keys.rig.run("midi.audition", key).at("bytes").at(0) == 0x92);
}

TEST_CASE("a project saved before notes had ids opens with the same notes and playback") {
    NoteRig keys;
    keys.add(60, 0, 480, 90);
    const model::Document document = keys.rig.controller->project().document();
    nlohmann::json old = model::toJson(document);
    old["format"] = 3;
    auto& clip = old["tracks"][0]["clips"][0];
    clip.erase("notes");
    clip["events"] = {{{"at", 0}, {"bytes", {0x90, 60, 90}}}, {{"at", 480}, {"bytes", {0x80, 60, 0}}}};
    const model::Document loaded = model::documentFromJson(old);
    const auto& notes = std::get<model::MidiClip>(loaded.tracks[0].clips[0]).notes;
    REQUIRE(notes.size() == 1);
    CHECK((notes[0].pitch == 60 && notes[0].velocity == 90 && notes[0].length == 480));
    CHECK(toSession(loaded, keys.rig.registry, {}).tracks.at(0).midiClips.at(0).events ==
          toSession(document, keys.rig.registry, {}).tracks.at(0).midiClips.at(0).events);
}

TEST_CASE("the piano roll opens on the selected MIDI clip and state shows notes only on request") {
    NoteRig keys;
    keys.add(60, 0);
    keys.rig.run("selection.set", {{"clips", {keys.clip}}});
    const Json opened = keys.rig.run("view.togglePianoRoll");
    CHECK(opened.at("pianoRollOpen") == true);
    CHECK(opened.at("pianoRollClip") == keys.clip);
    const Json state = projectState(*keys.rig.controller).at("tracks").at(0).at("clips").at(0);
    CHECK(state.at("noteCount") == 1);
    CHECK_FALSE(state.contains("notes"));
    CHECK(keys.rig.run("project.clipEvents", {{"clip", keys.clip}}).at("notes").at(0).at("pitch") == 60);
    CHECK(keys.rig.run("midi.audition", {{"track", keys.track}, {"pitch", 60}}).at("sent") == false);
    CHECK(keys.rig.run("view.togglePianoRoll").at("pianoRollOpen") == false);
}
