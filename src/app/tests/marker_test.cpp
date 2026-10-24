#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "midi_file.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

constexpr core::Ticks kBar = 3840;

std::vector<std::string> markerNames(const ProjectRig& rig) {
    std::vector<std::string> names;
    for (const model::Marker& marker : rig.controller->project().document().markers) {
        names.push_back(marker.name);
    }
    return names;
}

}  // namespace

TEST_CASE("markers add at the playhead or a position, rename, move and delete with one undo step each") {
    ProjectRig rig;
    rig.controller->locate(2 * kBar);
    const std::string verse = rig.run("marker.add", {{"name", "Verse"}}).at("id");
    CHECK(rig.nav.selectedMarker().toStdString() == verse);
    const Json intro = rig.run("marker.add", {{"at", "1.1.0"}, {"name", "Intro"}, {"color", 2}});
    CHECK(intro.at("number") == 1);
    CHECK(intro.at("color") == 2);
    CHECK(markerNames(rig) == std::vector<std::string>{"Intro", "Verse"});
    CHECK(rig.run("marker.add").at("name") == "Marker 3");
    CHECK(projectState(*rig.controller).at("markers").size() == 3);

    rig.run("marker.rename", {{"marker", verse}, {"name", "Chorus"}});
    CHECK(rig.controller->project().undoName() == "rename marker to Chorus");
    rig.run("marker.move", {{"marker", verse}, {"at", 4 * kBar}});
    CHECK(model::findMarker(rig.controller->project().document(), model::MarkerId{verse})->at == 4 * kBar);
    rig.run("project.undo");
    CHECK(model::findMarker(rig.controller->project().document(), model::MarkerId{verse})->at == 2 * kBar);

    rig.run("marker.select", {{"marker", verse}});
    rig.run("edit.delete");
    CHECK(model::findMarker(rig.controller->project().document(), model::MarkerId{verse}) == nullptr);
    rig.run("project.undo");
    CHECK(markerNames(rig) == std::vector<std::string>{"Intro", "Chorus", "Marker 3"});

    CHECK_THROWS_AS(rig.run("marker.move", {{"marker", verse}, {"at", -1}}), AppError);
    CHECK_THROWS_AS(rig.run("marker.rename", {{"marker", "nope"}, {"name", "x"}}), AppError);
    CHECK_THROWS_AS(rig.run("marker.add", {{"color", 9}}), AppError);
}

TEST_CASE("selecting a marker deselects clips and selecting clips deselects the marker") {
    ProjectRig rig;
    const std::string marker = rig.run("marker.add").at("id");
    rig.nav.setSelectedClips({"some clip"});
    CHECK(rig.nav.selectedMarker().isEmpty());
    rig.run("marker.select", {{"marker", marker}});
    CHECK(rig.nav.selectedClips().isEmpty());
}

TEST_CASE("the marker keys go to the next, previous and numbered markers") {
    ProjectRig rig;
    rig.run("marker.add", {{"at", kBar}, {"name", "A"}});
    rig.run("marker.add", {{"at", 3 * kBar}, {"name", "B"}});
    rig.controller->locate(0);

    CHECK(rig.run("marker.next").at("marker").at("name") == "A");
    CHECK(rig.controller->positionTicks() == kBar);
    rig.run("marker.next");
    CHECK(rig.controller->positionTicks() == 3 * kBar);
    CHECK_THROWS_AS(rig.run("marker.next"), AppError);
    rig.controller->locate(3 * kBar + 10);
    rig.run("marker.previous");
    CHECK(rig.controller->positionTicks() == 3 * kBar);
    rig.run("marker.previous");
    CHECK(rig.controller->positionTicks() == kBar);
    CHECK_THROWS_AS(rig.run("marker.previous"), AppError);

    rig.run("marker.goto2");
    CHECK(rig.controller->positionTicks() == 3 * kBar);
    rig.run("marker.goto", {{"number", 1}});
    CHECK(rig.controller->positionTicks() == kBar);
    CHECK_THROWS_AS(rig.run("marker.goto3"), AppError);
    CHECK(rig.registry.action("marker.add")->shortcut == "Alt+M");
    CHECK(rig.registry.action("marker.next")->shortcut == "Alt+.");
    CHECK(rig.registry.action("marker.goto1")->shortcut == "1");
}

TEST_CASE("markers survive saving and opening the project") {
    ProjectRig rig;
    rig.run("marker.add", {{"at", kBar}, {"name", "Bridge"}, {"color", 4}});
    rig.controller->saveAs(rig.path("Song"));
    rig.controller->open(rig.path("Song.erk"));
    REQUIRE(rig.controller->project().document().markers.size() == 1);
    CHECK(rig.controller->project().document().markers.front().name == "Bridge");
    CHECK(rig.controller->project().document().markers.front().color == 4);
}

TEST_CASE("export for Logic writes the project's markers into the MIDI file") {
    ProjectRig rig;
    rig.run("track.addMidi", {{"name", "Keys"}});
    rig.run("marker.add", {{"at", 0}, {"name", "Intro"}});
    rig.run("marker.add", {{"at", kBar}, {"name", "Verse"}});
    rig.run("export.logic", {{"folder", rig.path("out").string()}});
    const libremidi::reader midi = readMidiFile(rig.path("out") / "Untitled 1 for Logic" / "MIDI.mid");
    CHECK(metaTexts(midi.tracks.at(0), libremidi::meta_event_type::MARKER) ==
          std::vector<std::string>{"Intro", "Verse"});
}
