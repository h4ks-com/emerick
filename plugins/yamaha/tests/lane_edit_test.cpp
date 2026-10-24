#include <catch2/catch_test_macros.hpp>
#include <string>
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
        track.midiIn = {.portId = app::test::kMidiPort, .channel = 0, .thru = false, .sysex = false};
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

}  // namespace

TEST_CASE("a block's context menu changes, splits and deletes it") {
    YamahaRig rig;
    CHECK(menuTitles(rig.registry, "Chord context") ==
          std::vector<QString>{"Change chord", "Split chord", "Delete chord"});
    CHECK(menuTitles(rig.registry, "Section context") ==
          std::vector<QString>{"Change section", "Split section", "Delete section"});

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
