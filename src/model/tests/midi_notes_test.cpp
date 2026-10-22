#include "emerick/model/midi_notes.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <variant>
#include <vector>

#include "emerick/model/json.hpp"

using namespace emerick;
using namespace emerick::model;

namespace {

MidiClip recorded() {
    MidiClip clip{.start = 960, .length = 1920};
    clip.events = {{0, {0x90, 60, 100}},  {100, {0x91, 64, 80}}, {200, {0xB0, 64, 127}},
                   {600, {0x90, 67, 90}}, {1000, {0x80, 60, 0}}, {1800, {0x91, 64, 0}}};
    return clip;
}

}  // namespace

TEST_CASE("gathering notes pairs each note-on with its note-off and holds the rest to the clip end") {
    MidiClip clip = recorded();
    gatherNotes(clip);
    REQUIRE(clip.notes.size() == 3);
    CHECK((clip.notes[0].pitch == 60 && clip.notes[0].start == 0 && clip.notes[0].length == 1000));
    CHECK((clip.notes[0].velocity == 100 && clip.notes[0].channel == 1));
    CHECK((clip.notes[1].pitch == 64 && clip.notes[1].length == 1700 && clip.notes[1].channel == 2));
    CHECK((clip.notes[2].pitch == 67 && clip.notes[2].length == 1320));
    CHECK(clip.events == std::vector<MidiEvent>{{200, {0xB0, 64, 127}}});
    CHECK(clip.notes[0].id != clip.notes[1].id);
}

TEST_CASE("notes become the same note-ons and note-offs again, merged with the other events") {
    MidiClip clip = recorded();
    gatherNotes(clip);
    CHECK(clipEvents(clip) == std::vector<MidiEvent>{{0, {0x90, 60, 100}},
                                                     {100, {0x91, 64, 80}},
                                                     {200, {0xB0, 64, 127}},
                                                     {600, {0x90, 67, 90}},
                                                     {1000, {0x80, 60, 0}},
                                                     {1800, {0x81, 64, 0}},
                                                     {1920, {0x80, 67, 0}}});
}

TEST_CASE("a note-off goes before a note-on at the same tick and notes outside the clip stay silent") {
    MidiClip clip{.length = 960};
    clip.notes = {{.pitch = 60, .start = 480, .length = 480},
                  {.pitch = 60, .start = 0, .length = 480},
                  {.pitch = 62, .start = 960, .length = 10},
                  {.pitch = 64, .start = 900, .length = 960}};
    CHECK(clipEvents(clip) == std::vector<MidiEvent>{{0, {0x90, 60, 100}},
                                                     {480, {0x80, 60, 0}},
                                                     {480, {0x90, 60, 100}},
                                                     {900, {0x90, 64, 100}},
                                                     {960, {0x80, 60, 0}},
                                                     {960, {0x80, 64, 0}}});
}

TEST_CASE("a project from before format 4 gathers its notes from the events") {
    nlohmann::json json = toJson(Document{.tracks = {Track{.kind = "midi", .clips = {recorded()}}}});
    json["format"] = 3;
    json["tracks"][0]["clips"][0].erase("notes");
    const Document loaded = documentFromJson(json);
    const auto& clip = std::get<MidiClip>(loaded.tracks[0].clips[0]);
    CHECK(clip.notes.size() == 3);
    CHECK(clip.events.size() == 1);
    CHECK(documentFromJson(toJson(loaded)) == loaded);
}

TEST_CASE("quantizing moves a position toward the grid by the strength") {
    CHECK(quantized(130, 240, 1.0) == 240);
    CHECK(quantized(100, 240, 1.0) == 0);
    CHECK(quantized(200, 240, 0.5) == 220);
    CHECK(quantized(200, 240, 0.0) == 200);
    CHECK(quantized(200, 240, 3.0) == 240);
    CHECK(quantized(200, 0, 1.0) == 200);
}
