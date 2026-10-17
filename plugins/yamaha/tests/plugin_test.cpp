#include <catch2/catch_test_macros.hpp>
#include <ranges>
#include <string>
#include <vector>

#include "../src/lane_blocks.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/yamaha/chart.hpp"
#include "yamaha_rig.hpp"

using namespace emerick;
using namespace emerick::yamaha;
using namespace emerick::yamaha::test;

namespace {

constexpr core::Ticks kBar = 4 * core::kTicksPerQuarter;

engine::RecordedTake takeOf(const std::string& trackId, core::Ticks start, const std::vector<RawEvent>& events) {
    engine::RecordedTake take{.trackId = trackId, .startTick = start};
    for (const RawEvent& event : events) {
        take.midiEvents.push_back({event.at - start, event.bytes});
    }
    return take;
}

void record(YamahaRig& rig, const std::string& id, const engine::RecordedTake& take, core::Ticks stop) {
    const app::TrackType& type = *rig.registry.trackType(rig.track(id).kind);
    rig.controller->project().editTrack(model::TrackId{id}, "take", [&](model::Track& track) {
        type.fromTake(track, take, {.tempo = rig.controller->project().document().tempo, .stopTicks = stop});
    });
}

engine::Track played(const YamahaRig& rig, const std::string& id) {
    return rig.registry.trackType(rig.track(id).kind)->toEngine(rig.track(id), {});
}

std::vector<Bytes> bytesOf(const engine::Track& track) {
    return track.midiClips.at(0).events | std::views::transform(&engine::MidiEvent::bytes) |
           std::ranges::to<std::vector>();
}

RawEvent chordSysex(core::Ticks at, std::uint8_t root, std::uint8_t type) {
    return {at, {0xF0, 0x43, 0x7E, 0x02, root, type, 0x7F, 0x7F, 0xF7}};
}

}  // namespace

TEST_CASE("a recorded take becomes the chord list and the section list") {
    YamahaRig rig;
    const std::string chords = rig.addTrack("yamaha.chord");
    const std::string style = rig.addTrack("yamaha.style");
    const auto events = takeSysex();
    const core::TempoMap tempo = rig.controller->project().document().tempo;
    const Chart expected = chartFromTake(events, tempo, {});

    record(rig, chords, takeOf(chords, 0, events), events.back().at + 1);
    record(rig, style, takeOf(style, 0, events), events.back().at + 1);
    CHECK(rig.track(chords).data == Json(expected.chords));
    CHECK(rig.track(style).data == Json(expected.sections));
    CHECK(rig.track(chords).data.at(1).at("chord") == "Am");
}

TEST_CASE("a take replaces only the chords it covers") {
    YamahaRig rig;
    const std::string id = rig.addTrack("yamaha.chord");
    rig.run("yamaha.chord.set", {{"track", id}, {"at", 0}, {"chord", "C"}});
    rig.run("yamaha.chord.set", {{"track", id}, {"at", 2 * kBar}, {"chord", "F"}});
    rig.run("yamaha.chord.set", {{"track", id}, {"at", 4 * kBar}, {"chord", "G"}});

    record(rig, id, takeOf(id, 2 * kBar, {chordSysex(2 * kBar, 0x36, 0x08)}), 3 * kBar);
    const Json data = rig.track(id).data;
    REQUIRE(data.size() == 3);
    CHECK(data.at(0).at("chord") == "C");
    CHECK(data.at(1).at("chord") == "Am");
    CHECK(data.at(2).at("chord") == "G");
}

TEST_CASE("the snap and anticipation settings shape recorded chords") {
    YamahaRig rig;
    const std::string id = rig.addTrack("yamaha.chord");
    const core::Ticks offBeat = 3 * core::kTicksPerQuarter + 100;
    const auto take = takeOf(id, 0, {chordSysex(offBeat, 0x31, 0x00)});

    record(rig, id, take, kBar);
    CHECK(rig.track(id).data.at(0).at("at") == kBar);
    rig.registry.setSetting("yamaha.anticipation", false);
    record(rig, id, take, kBar);
    CHECK(rig.track(id).data.at(0).at("at") == 3 * core::kTicksPerQuarter);
    rig.registry.setSetting("yamaha.snapToBeat", false);
    record(rig, id, take, kBar);
    CHECK(rig.track(id).data.at(0).at("at") == offBeat);
}

TEST_CASE("a chord track plays block chords on its MIDI out channel") {
    YamahaRig rig;
    const std::string id = rig.addTrack("yamaha.chord");
    rig.run("track.setMidiOut", {{"track", id}, {"port", app::test::kMidiPort}, {"channel", 4}});
    rig.run("yamaha.chord.set", {{"track", id}, {"at", 0}, {"chord", "C"}});
    rig.run("yamaha.chord.set", {{"track", id}, {"at", kBar}, {"chord", "N.C."}});
    rig.run("yamaha.chord.set", {{"track", id}, {"at", 2 * kBar}, {"chord", "G7"}});

    const engine::Track track = played(rig, id);
    CHECK(track.kind == engine::TrackKind::midi);
    CHECK(track.midiOut.portId == app::test::kMidiPort);
    const auto& events = track.midiClips.at(0).events;
    REQUIRE(events.size() == 14);
    CHECK(events.at(0) == engine::MidiEvent{0, {0x93, 48, 90}});
    CHECK(events.at(3).tick == kBar);
    CHECK(events.at(3).bytes.at(0) == 0x83);
    CHECK(events.at(6).tick == 2 * kBar);
    CHECK(events.back().tick == 3 * kBar);
}

TEST_CASE("the chord detect channel setting applies when the route has no channel") {
    YamahaRig rig;
    const std::string id = rig.addTrack("yamaha.chord");
    rig.run("yamaha.chord.set", {{"track", id}, {"at", 0}, {"chord", "C"}});
    CHECK(bytesOf(played(rig, id)).at(0).at(0) == 0x93);
    rig.registry.setSetting("yamaha.chordDetectChannel", 5);
    CHECK(bytesOf(played(rig, id)).at(0).at(0) == 0x94);
}

TEST_CASE("a style track presses section buttons on its MIDI out channel") {
    YamahaRig rig;
    const std::string id = rig.addTrack("yamaha.style");
    rig.run("yamaha.section.set", {{"track", id}, {"at", 0}, {"kind", "main"}, {"variation", 1}});
    rig.run("yamaha.section.set", {{"track", id}, {"at", kBar}, {"name", "Ending 2"}});
    CHECK(bytesOf(played(rig, id)) ==
          std::vector<Bytes>{{0xB0, 12, 127}, {0xB0, 12, 0}, {0xB0, 32, 127}, {0xB0, 32, 0}});
    rig.registry.setSetting("yamaha.controllerChannel", 2);
    CHECK(bytesOf(played(rig, id)).at(0).at(0) == 0xB1);
}

TEST_CASE("chord actions set, move and delete with undo and redo") {
    YamahaRig rig;
    const std::string id = rig.addTrack("yamaha.chord");
    Json result = rig.run("yamaha.chord.set", {{"track", id}, {"at", "1.3.0"}, {"chord", "F#m7"}});
    CHECK(result.at("data") == Json::parse(R"([{"at":1920,"chord":"F#m7"}])"));
    CHECK(result.at("chart") == "    1.3  F#m7\n");
    rig.run("yamaha.chord.set", {{"track", id}, {"at", 1920}, {"chord", "Bbmaj7/D"}});
    CHECK(rig.track(id).data.at(0).at("chord") == "Bbmaj7/D");

    rig.run("yamaha.chord.move", {{"track", id}, {"at", 1920}, {"to", "2.1.0"}});
    CHECK(rig.track(id).data.at(0).at("at") == kBar);
    rig.run("yamaha.chord.delete", {{"track", id}, {"at", kBar}});
    CHECK(rig.track(id).data.empty());

    rig.run("project.undo");
    CHECK(rig.track(id).data.at(0).at("at") == kBar);
    rig.run("project.undo");
    CHECK(rig.track(id).data.at(0).at("at") == 1920);
    rig.run("project.undo");
    CHECK(rig.track(id).data.at(0).at("chord") == "F#m7");
    rig.run("project.redo");
    rig.run("project.redo");
    CHECK(rig.track(id).data.at(0).at("at") == kBar);
    CHECK(rig.controller->project().undoName() == "move chord");
}

TEST_CASE("chord actions reject bad names, missing chords and other track kinds") {
    YamahaRig rig;
    const std::string id = rig.addTrack("yamaha.chord");
    const std::string audio = rig.run("track.addAudio").at("id");
    CHECK_THROWS_AS(rig.run("yamaha.chord.set", {{"track", id}, {"at", 0}, {"chord", "Hm"}}), app::AppError);
    try {
        rig.run("yamaha.chord.set", {{"track", id}, {"at", 0}, {"chord", "Xyz"}});
    } catch (const app::AppError& error) {
        CHECK(std::string{error.what()}.contains("such as C, F#m7"));
    }
    CHECK_THROWS_AS(rig.run("yamaha.chord.move", {{"track", id}, {"at", 0}, {"to", 960}}), app::AppError);
    CHECK_THROWS_AS(rig.run("yamaha.chord.delete", {{"track", id}, {"at", 0}}), app::AppError);
    CHECK_THROWS_AS(rig.run("yamaha.chord.set", {{"track", audio}, {"at", 0}, {"chord", "C"}}), app::AppError);
    CHECK_THROWS_AS(rig.run("yamaha.chord.set", {{"track", id}, {"at", -5}, {"chord", "C"}}), app::AppError);
    CHECK_FALSE(rig.controller->project().undoName() == "set chord C");
}

TEST_CASE("section actions set, move and delete with undo and redo") {
    YamahaRig rig;
    const std::string id = rig.addTrack("yamaha.style");
    rig.run("yamaha.section.set", {{"track", id}, {"at", 0}, {"name", "Intro 1"}});
    rig.run("yamaha.section.set", {{"track", id}, {"at", kBar}, {"kind", "main"}});
    CHECK(rig.run("yamaha.chart.text", {{"track", id}}).at("chart") == "    1.1  [Intro 1]\n    2.1  [Main A]\n");
    rig.run("yamaha.section.move", {{"track", id}, {"at", kBar}, {"to", 2 * kBar}});
    rig.run("yamaha.section.delete", {{"track", id}, {"at", 0}});
    CHECK(rig.track(id).data == Json::parse(R"([{"at":7680,"kind":"main","variation":0}])"));

    rig.run("project.undo");
    rig.run("project.undo");
    CHECK(rig.track(id).data.at(1).at("at") == kBar);
    rig.run("project.redo");
    CHECK(rig.track(id).data.at(1).at("at") == 2 * kBar);

    CHECK_THROWS_AS(rig.run("yamaha.section.set", {{"track", id}, {"at", 0}, {"name", "Main E"}}), app::AppError);
    CHECK_THROWS_AS(rig.run("yamaha.section.set", {{"track", id}, {"at", 0}, {"kind", "main"}, {"variation", 4}}),
                    app::AppError);
    CHECK_THROWS_AS(rig.run("yamaha.section.set", {{"track", id}, {"at", 0}, {"kind", "verse"}}), app::AppError);
}

TEST_CASE("the lanes draw chords with N.C. as a gap and sections in their colors") {
    const QVariantList chords =
        LaneBlocks::chordBlocks(app::toVariant(Json::parse(R"([{"at":3840,"chord":"N.C."},{"at":0,"chord":"Dm7"}])")));
    REQUIRE(chords.size() == 2);
    CHECK(chords.at(0).toMap().value("label").toString() == "Dm7");
    CHECK(chords.at(0).toMap().value("end").toLongLong() == kBar);
    CHECK(chords.at(1).toMap().value("gap").toBool());
    CHECK(chords.at(1).toMap().value("end").toLongLong() == 2 * kBar);

    const QVariantList sections = LaneBlocks::sectionBlocks(app::toVariant(
        Json::parse(R"([{"at":0,"kind":"main","variation":2},{"at":3840,"kind":"ending","variation":0}])")));
    CHECK(sections.at(0).toMap().value("label").toString() == "Main C");
    CHECK(sections.at(0).toMap().value("colorIndex").toInt() == 2);
    CHECK(sections.at(1).toMap().value("colorIndex").toInt() == 7);
    CHECK(LaneBlocks::chordBlocks(app::toVariant(Json{{"bad", 1}})).isEmpty());
}
