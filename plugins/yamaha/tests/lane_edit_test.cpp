#include <QVariantMap>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>
#include <vector>

#include "../src/lane_blocks.hpp"
#include "emerick/app/registry.hpp"
#include "yamaha_rig.hpp"

using namespace emerick;
using namespace emerick::yamaha;
using namespace emerick::yamaha::test;

namespace {

constexpr core::Ticks kBar = 4 * core::kTicksPerQuarter;

std::vector<QString> menuTitles(const app::Registry& registry, const QString& menu) {
    std::vector<QString> titles;
    for (const QVariant& entry : registry.menus().value(menu).toList()) {
        titles.push_back(entry.toMap().value("title").toString());
    }
    return titles;
}

std::string chordTrackOnKeyboard(YamahaRig& rig) {
    const std::string id = rig.addTrack("yamaha.chord");
    rig.controller->project().editTrack(model::TrackId{id}, "route", [](model::Track& track) {
        track.midiIn = {.portId = app::test::kMidiPort, .channel = 0, .thru = false, .sysex = false, .sourceTrack = {}};
    });
    return id;
}

void play(const YamahaRig& rig, const std::vector<Bytes>& messages) {
    for (const Bytes& bytes : messages) {
        rig.midi->inject(app::test::kMidiPort, bytes, 0);
    }
}

bool chordsAre(const YamahaRig& rig, const std::string& id, const Json& expected) {
    return processUntil([&] { return rig.track(id).data == expected; }, 1000);
}

std::string threeChords(YamahaRig& rig) {
    const std::string id = rig.addTrack("yamaha.chord");
    rig.run("yamaha.chord.set", {{"track", id}, {"at", 0}, {"chord", "C"}});
    rig.run("yamaha.chord.set", {{"track", id}, {"at", kBar}, {"chord", "F"}});
    rig.run("yamaha.chord.set", {{"track", id}, {"at", 2 * kBar}, {"chord", "G"}});
    return id;
}

std::pair<core::Ticks, core::Ticks> selectedRange(const YamahaRig& rig) {
    const QVariantMap selected = rig.nav.selectedBlocks();
    return {selected.value("from").toLongLong(), selected.value("to").toLongLong()};
}

}  // namespace

TEST_CASE("a block's context menu changes, splits and deletes it") {
    YamahaRig rig;
    CHECK(menuTitles(rig.registry, "Chord context") ==
          std::vector<QString>{"Change chord", "Split chord", "Delete chord", "Copy chords", "Paste chords",
                               "Duplicate chords", "Loop selected chords"});
    CHECK(menuTitles(rig.registry, "Section context") ==
          std::vector<QString>{"Change section", "Split section", "Delete section", "Copy sections", "Paste sections",
                               "Duplicate sections", "Loop selected sections", "Set pattern length"});

    const std::string id = rig.addTrack("yamaha.chord");
    rig.run("yamaha.chord.set", {{"track", id}, {"at", 0}, {"chord", "C"}});
    rig.run("yamaha.chord.set", {{"track", id}, {"at", kBar}, {"chord", "G7"}});
    rig.run("yamaha.chord.edit", {{"track", id}, {"at", kBar}});
    CHECK(rig.nav.editingBlock().value("track").toString().toStdString() == id);
    CHECK(rig.nav.editingBlock().value("at").toLongLong() == kBar);

    rig.run("yamaha.chord.split", {{"track", id}, {"at", 0}, {"to", "1.3.0"}});
    CHECK(rig.track(id).data ==
          Json::parse(R"([{"at":0,"chord":"C"},{"at":1920,"chord":"C"},{"at":3840,"chord":"G7"}])"));
    CHECK(rig.controller->project().undoName() == "split chord");
    CHECK_THROWS_AS(rig.run("yamaha.chord.split", {{"track", id}, {"at", 0}, {"to", 1920}}), app::AppError);
    CHECK_THROWS_AS(rig.run("yamaha.chord.split", {{"track", id}, {"at", 1920}, {"to", 1920}}), app::AppError);
    rig.run("yamaha.chord.split", {{"track", id}, {"at", kBar}, {"to", 2 * kBar}});
    CHECK(rig.track(id).data.size() == 4);
}

TEST_CASE("the pickers offer roots, chord types, bass notes and the sections a style plays") {
    const QVariantList qualities = LaneBlocks::chordQualities();
    CHECK(qualities.size() == static_cast<qsizetype>(chordTypes().size()));
    CHECK(qualities.front().toMap() == QVariantMap{{"id", ""}, {"name", "maj"}});
    CHECK(qualities.back().toMap().value("id").toString() == "N.C.");
    CHECK(LaneBlocks::chordRoots().size() == 17);
    CHECK(LaneBlocks::chordBasses().front().toMap().value("name").toString() == "No bass");

    const QVariantList sections = LaneBlocks::sectionChoices();
    CHECK(sections.size() == 16);
    CHECK(sections.front().toMap().value("id").toString() == "Intro 1");
    CHECK(sections.contains(QVariantMap{{"id", "Main B (fill)"}, {"name", "Fill in to Main B"}}));
    CHECK(sections.back().toMap().value("id").toString() == "Ending 3");

    CHECK(LaneBlocks::chordParts("Bbm7/D") == QVariantMap{{"root", "Bb"}, {"quality", "m7"}, {"bass", "D"}});
    CHECK(LaneBlocks::chordParts("N.C.") == QVariantMap{{"root", ""}, {"quality", "N.C."}, {"bass", ""}});
    CHECK(LaneBlocks::chordParts("").value("root").toString().isEmpty());
}

TEST_CASE("while a chord block's picker is open, the next chord played on the track's input sets it") {
    YamahaRig rig;
    const std::string id = chordTrackOnKeyboard(rig);
    rig.run("yamaha.chord.edit", {{"track", id}, {"at", kBar}});

    play(rig, {{0x93, 57, 100}, {0x93, 60, 100}, {0x93, 64, 100}, {0x91, 40, 100}, {0x93, 67, 100}});
    play(rig, {{0x83, 57, 0}, {0x93, 60, 0}, {0x83, 64, 0}});
    CHECK_FALSE(chordsAre(rig, id, Json::parse(R"([{"at":3840,"chord":"Am7"}])")));
    play(rig, {{0x83, 67, 0}});
    CHECK(chordsAre(rig, id, Json::parse(R"([{"at":3840,"chord":"Am7"}])")));
    CHECK(rig.nav.editingBlock().isEmpty());

    play(rig, {{0x93, 60, 100}, {0x83, 60, 0}});
    CHECK_FALSE(chordsAre(rig, id, Json::parse(R"json([{"at":3840,"chord":"C(1+8)"}])json")));

    rig.run("yamaha.chord.edit", {{"track", id}, {"at", 0}});
    play(rig, {{0xF0, 0x43, 0x7E, 0x02, 0x31, 0x13, 0x7F, 0x7F, 0xF7}});
    CHECK(chordsAre(rig, id, Json::parse(R"([{"at":0,"chord":"C7"},{"at":3840,"chord":"Am7"}])")));
}

TEST_CASE("the chord input follows the track's channel and leaves other tracks' blocks alone") {
    YamahaRig rig;
    const std::string id = chordTrackOnKeyboard(rig);
    rig.controller->project().editTrack(model::TrackId{id}, "channel",
                                        [](model::Track& track) { track.midiIn.channel = 2; });
    const std::string style = rig.addTrack("yamaha.style");
    rig.run("yamaha.section.edit", {{"track", style}, {"at", 0}});
    play(rig, {{0x91, 62, 100}, {0x91, 65, 100}, {0x91, 69, 100}, {0x81, 62, 0}, {0x81, 65, 0}, {0x81, 69, 0}});
    CHECK_FALSE(chordsAre(rig, id, Json::parse(R"([{"at":0,"chord":"Dm"}])")));

    rig.run("yamaha.chord.edit", {{"track", id}, {"at", 0}});
    play(rig, {{0x93, 62, 100}, {0x83, 62, 0}});
    play(rig, {{0x91, 62, 100}, {0x91, 65, 100}, {0x91, 69, 100}, {0x81, 62, 0}, {0x81, 65, 0}, {0x81, 69, 0}});
    CHECK(chordsAre(rig, id, Json::parse(R"([{"at":0,"chord":"Dm"}])")));
}

TEST_CASE("selecting chords takes every block the range touches, from its start to its end") {
    YamahaRig rig;
    const std::string id = threeChords(rig);
    rig.run("yamaha.chord.select", {{"track", id}, {"from", kBar / 2}, {"to", kBar + (kBar / 2)}});
    CHECK(rig.nav.selectedBlocks().value("track").toString().toStdString() == id);
    CHECK(selectedRange(rig) == std::pair{core::Ticks{0}, 2 * kBar});
    rig.run("yamaha.chord.select", {{"track", id}, {"from", 2 * kBar}, {"to", (2 * kBar) + 1}});
    CHECK(selectedRange(rig) == std::pair{2 * kBar, 3 * kBar});
    rig.run("yamaha.chord.select", {{"track", id}});
    CHECK(rig.nav.selectedBlocks().isEmpty());
    CHECK_THROWS_AS(rig.run("yamaha.chord.copy", {{"track", id}}), app::AppError);
}

TEST_CASE("pasted chords replace the ones in their range and keep the chord that played after it") {
    YamahaRig rig;
    const std::string id = threeChords(rig);
    CHECK_THROWS_AS(rig.run("yamaha.chord.paste", {{"track", id}, {"at", 0}}), app::AppError);
    rig.run("yamaha.chord.copy", {{"track", id}, {"from", 0}, {"to", 2 * kBar}});
    rig.run("yamaha.chord.paste", {{"track", id}, {"at", 4 * kBar}});
    CHECK(rig.track(id).data == Json::parse(R"([{"at":0,"chord":"C"},{"at":3840,"chord":"F"},{"at":7680,"chord":"G"},
                          {"at":15360,"chord":"C"},{"at":19200,"chord":"F"},{"at":23040,"chord":"G"}])"));
    CHECK(rig.controller->project().undoName() == "paste chords");
    CHECK(selectedRange(rig) == std::pair{4 * kBar, 6 * kBar});

    const std::string style = rig.addTrack("yamaha.style");
    CHECK_THROWS_AS(rig.run("yamaha.section.paste", {{"track", style}, {"at", 0}}), app::AppError);
}

TEST_CASE("duplicating the selected chords repeats them right after themselves and selects the repeat") {
    YamahaRig rig;
    const std::string id = threeChords(rig);
    rig.run("yamaha.chord.select", {{"track", id}, {"from", 0}, {"to", kBar + 1}});
    rig.run("yamaha.chord.duplicate", {{"track", id}});
    CHECK(rig.track(id).data == Json::parse(R"([{"at":0,"chord":"C"},{"at":3840,"chord":"F"},{"at":7680,"chord":"C"},
                                                {"at":11520,"chord":"F"},{"at":15360,"chord":"G"}])"));
    CHECK(selectedRange(rig) == std::pair{2 * kBar, 4 * kBar});
    rig.run("project.undo");
    CHECK(rig.track(id).data.size() == 3);
}

TEST_CASE("duplicating the last chords adds no block that repeats the last one") {
    YamahaRig rig;
    const std::string id = threeChords(rig);
    rig.run("yamaha.chord.duplicate", {{"track", id}, {"from", kBar}, {"to", 3 * kBar}});
    CHECK(rig.track(id).data == Json::parse(R"([{"at":0,"chord":"C"},{"at":3840,"chord":"F"},{"at":7680,"chord":"G"},
                                                {"at":11520,"chord":"F"},{"at":15360,"chord":"G"}])"));
}

TEST_CASE("looping the selected chords sets the cycle to their range and turns it on") {
    YamahaRig rig;
    const std::string id = threeChords(rig);
    rig.run("yamaha.chord.select", {{"track", id}, {"from", kBar}, {"to", (2 * kBar) + 1}});
    rig.run("yamaha.chord.loop", {{"track", id}});
    const model::Cycle& cycle = rig.controller->project().document().cycle;
    CHECK(cycle.enabled);
    CHECK(cycle.start == kBar);
    CHECK(cycle.end == 3 * kBar);
    CHECK(rig.controller->project().undoName() == "loop chords");
}

TEST_CASE("moving a range moves every chord in it by the same distance as one undo step") {
    YamahaRig rig;
    const std::string id = threeChords(rig);
    rig.run("yamaha.chord.move", {{"track", id}, {"at", 0}, {"to", 3 * kBar}, {"end", 2 * kBar}});
    CHECK(rig.track(id).data ==
          Json::parse(R"([{"at":7680,"chord":"G"},{"at":11520,"chord":"C"},{"at":15360,"chord":"F"}])"));
    CHECK(rig.controller->project().undoName() == "move chords");
    CHECK(selectedRange(rig) == std::pair{3 * kBar, 5 * kBar});
    rig.run("project.undo");
    CHECK(rig.track(id).data.at(0).at("chord") == "C");
    CHECK_THROWS_AS(rig.run("yamaha.chord.move", {{"track", id}, {"at", 1}, {"to", 2}, {"end", 3}}), app::AppError);
}

TEST_CASE("deleting without a position deletes the selected chords") {
    YamahaRig rig;
    const std::string id = threeChords(rig);
    rig.run("yamaha.chord.select", {{"track", id}, {"from", 0}, {"to", kBar + 1}});
    rig.run("yamaha.chord.delete", {{"track", id}});
    CHECK(rig.track(id).data == Json::parse(R"([{"at":7680,"chord":"G"}])"));
    CHECK(rig.nav.selectedBlocks().isEmpty());
    CHECK_THROWS_AS(rig.run("yamaha.chord.delete", {{"track", id}}), app::AppError);
}
