#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/project_files.hpp"
#include "emerick/app/stem_export.hpp"
#include "emerick/app/view_state.hpp"
#include "emerick/model/clip_edit.hpp"
#include "emerick/model/json.hpp"
#include "midi_file.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;
using Catch::Approx;

namespace {

Json savedProject(const std::filesystem::path& folder) {
    std::ifstream file(folder / "project.json");
    return Json::parse(file);
}

// Writes the project file the way another program saving it would, with a later modification time.
void saveElsewhere(const std::filesystem::path& folder, double bpm) {
    Json project = savedProject(folder);
    project["tempo"]["bpm"] = bpm;
    std::ofstream(folder / "project.json") << project.dump();
    const auto file = folder / "project.json";
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds{2});
}

double tempoOf(const ProjectRig& rig) { return rig.run("project.summary").at("tempo").at("bpm").get<double>(); }

std::vector<std::string> recent(const ProjectRig& rig) {
    return rig.registry.settingValue("project.recent").get<std::vector<std::string>>();
}

model::Track pluginTrack(const std::string& pluginId, const std::string& state) {
    model::Track track;
    track.instrument = model::PluginSlot{.pluginId = pluginId,
                                         .bundle = {},
                                         .name = pluginId,
                                         .state = {state.begin(), state.end()},
                                         .format = core::PluginFormat::clap};
    return track;
}

std::string stateOf(const model::Track& track) {
    return {track.instrument->state.begin(), track.instrument->state.end()};
}

}  // namespace

TEST_CASE("a new project starts as an Untitled bundle in the projects folder") {
    ProjectRig rig;
    const auto first = rig.path("Projects") / "Untitled 1.erk";
    CHECK(rig.controller->folder() == first);
    CHECK(rig.controller->name() == "Untitled 1");
    CHECK(std::filesystem::exists(first / "project.json"));
    CHECK(std::filesystem::is_directory(first / "audio"));

    rig.run("project.new");
    CHECK(rig.controller->folder() == first);

    rig.run("project.setTempo", {{"bpm", 96}});
    rig.run("project.new");
    CHECK(savedProject(first).at("tempo").at("bpm") == 96.0);
    rig.run("project.open", {{"path", first.string()}});

    rig.run("track.addAudio", {{"name", "Vox"}});
    const Json state = rig.run("project.new");
    CHECK(state.at("path") == (rig.path("Projects") / "Untitled 2.erk").string());
    CHECK(state.at("untitled") == true);
    CHECK(savedProject(first).at("tracks").at(0).at("name") == "Vox");
}

TEST_CASE("save as moves an untitled bundle, copies a named one, and the bundle opens again") {
    ProjectRig rig;
    const std::string id = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    writeConstant(rig.path("ones.wav"), 4800, 1.0F);
    rig.run("clip.import", {{"track", id}, {"path", rig.path("ones.wav").string()}});
    const auto untitled = rig.controller->folder();

    const Json saved = rig.run("project.saveAs", {{"path", rig.path("Songs/Take").string()}});
    const auto song = rig.path("Songs/Take.erk");
    CHECK(saved.at("path") == song.string());
    CHECK(saved.at("untitled") == false);
    CHECK(rig.controller->name() == "Take");
    CHECK_FALSE(std::filesystem::exists(untitled));
    CHECK(std::filesystem::exists(song / "audio" / "ones.wav"));
    const Json before = model::toJson(rig.controller->project().document());

    rig.run("project.saveAs", {{"path", rig.path("Songs/Copy.erk").string()}});
    CHECK(std::filesystem::exists(song / "project.json"));
    CHECK(std::filesystem::exists(rig.path("Songs/Copy.erk") / "audio" / "ones.wav"));
    CHECK_THROWS_AS(rig.run("project.saveAs", {{"path", song.string()}}), AppError);

    rig.run("project.new");
    const Json opened = rig.run("project.open", {{"path", (song / "project.json").string()}});
    CHECK(opened.at("path") == song.string());
    CHECK(model::toJson(rig.controller->project().document()) == before);
    CHECK(rig.controller->edited() == false);
}

TEST_CASE("bundled plugins save paths beside their bundle relatively, and other paths and plugins stay as they are") {
    ProjectRig rig;
    const std::filesystem::path work = rig.path("Work");
    const std::filesystem::path bundle = work / "Song.erk";
    const auto json = [](const std::string& key, const std::filesystem::path& path) {
        return Json{{key, path.string()}, {"sound", 3}}.dump();
    };
    model::Document document;
    document.tracks = {pluginTrack(kGeneralMidiPluginId, json("soundFont", work / "Piano.sf2")),
                       pluginTrack(kVoicesPluginId, json("folder", bundle / "voices")),
                       pluginTrack(kArrangerPluginId, json("style", rig.path("Styles/Pop.sty"))),
                       pluginTrack("com.example.synth", json("style", work / "Pop.sty")),
                       pluginTrack(kGeneralMidiPluginId, (work / "Plain.sf2").string())};

    const model::Document saved = withRelativePluginPaths(document, bundle);
    CHECK(Json::parse(stateOf(saved.tracks[0])) == Json{{"soundFont", "../Piano.sf2"}, {"sound", 3}});
    CHECK(Json::parse(stateOf(saved.tracks[1])).at("folder") == "voices");
    CHECK(stateOf(saved.tracks[2]) == stateOf(document.tracks[2]));
    CHECK(stateOf(saved.tracks[3]) == stateOf(document.tracks[3]));
    CHECK(stateOf(saved.tracks[4]) == stateOf(document.tracks[4]));

    const std::filesystem::path moved = rig.path("Moved/Song.erk");
    const model::Document opened = withAbsolutePluginPaths(saved, moved);
    CHECK(Json::parse(stateOf(opened.tracks[0])).at("soundFont") == rig.path("Moved/Piano.sf2").string());
    CHECK(Json::parse(stateOf(opened.tracks[1])).at("folder") == (moved / "voices").string());
    CHECK(withAbsolutePluginPaths(saved, bundle) == document);
}

TEST_CASE("a new project can start saved at a path with a tempo") {
    ProjectRig rig;
    const auto song = rig.path("Songs/Funk.erk");
    const Json state = rig.run("project.new", {{"path", rig.path("Songs/Funk").string()}, {"bpm", 120}});
    CHECK(state.at("path") == song.string());
    CHECK(state.at("untitled") == false);
    CHECK(savedProject(song).at("tempo").at("bpm") == 120.0);
    CHECK_THROWS_AS(rig.run("project.new", {{"path", song.string()}}), AppError);
}

TEST_CASE("autosave leaves a file another program saved, and opening the open project reloads it") {
    ProjectRig rig;
    const auto song = rig.path("Songs/Funk.erk");
    rig.run("project.new", {{"path", song.string()}, {"bpm", 120}});
    rig.run("project.setTempo", {{"bpm", 100}});
    saveElsewhere(song, 140);
    rig.run("project.open", {{"path", song.string()}});
    CHECK(tempoOf(rig) == 140.0);

    rig.run("project.setTempo", {{"bpm", 100}});
    saveElsewhere(song, 150);
    rig.run("project.new");
    CHECK(savedProject(song).at("tempo").at("bpm") == 150.0);

    rig.run("project.open", {{"path", song.string()}});
    rig.run("project.open", {{"path", song.string()}});
    CHECK(tempoOf(rig) == 150.0);
}

TEST_CASE("with autosave off, the file keeps what was last saved") {
    ProjectRig rig;
    rig.registry.setSetting("project.autosave", false);
    const auto song = rig.path("Songs/Funk.erk");
    rig.run("project.new", {{"path", song.string()}, {"bpm", 120}});
    rig.run("project.setTempo", {{"bpm", 100}});
    rig.run("project.new");
    CHECK(savedProject(song).at("tempo").at("bpm") == 120.0);

    rig.run("project.open", {{"path", song.string()}});
    rig.run("project.setTempo", {{"bpm", 90}});
    rig.run("project.save");
    CHECK(savedProject(song).at("tempo").at("bpm") == 90.0);
}

TEST_CASE("autosave writes project.json about two seconds after a take") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    rig.run("track.setArm", {{"track", vox}, {"value", true}});
    rig.run("transport.record");
    rig.audio->setInput("interface", 1, std::vector<float>(4800, 0.5F));
    rig.audio->process(4800);
    rig.run("transport.stop");
    REQUIRE(rig.waitForTakes(1) == 1);
    CHECK(rig.controller->edited());

    const auto folder = rig.controller->folder();
    CHECK(processUntil(
        [&] {
            const Json tracks = savedProject(folder).at("tracks");
            return tracks.size() == 1 && tracks.at(0).at("clips").size() == 1;
        },
        5000));
    CHECK_FALSE(rig.controller->edited());
}

TEST_CASE("the recent list keeps the last ten projects that still exist, newest first") {
    ProjectRig rig;
    for (int index = 0; index < 11; ++index) {
        rig.run("track.addMidi");
        rig.run("project.saveAs", {{"path", rig.path("song" + std::to_string(index)).string()}});
    }
    const auto list = recent(rig);
    REQUIRE(list.size() == 10);
    CHECK(list.front() == rig.path("song10.erk").string());
    CHECK(list.back() == rig.path("song1.erk").string());

    std::filesystem::remove_all(rig.path("song9.erk"));
    const QVariantList choices = rig.registry.menus().value("File").toList().at(2).toMap().value("choices").toList();
    REQUIRE(choices.size() == 9);
    CHECK(choices.at(0).toMap().value("title") == "song10");
    CHECK(choices.at(1).toMap().value("params").toMap().value("path") == qs(rig.path("song8.erk").string()));

    CHECK(std::ranges::none_of(rig.registry.palette("recent"),
                               [](const QVariant& hit) { return hit.toMap().value("id") == "project.openRecent"; }));

    rig.run("project.openRecent", {{"path", rig.path("song3.erk").string()}});
    CHECK(recent(rig).front() == rig.path("song3.erk").string());
}

TEST_CASE("an agent gets an error naming the missing path, and the UI gets a dialog") {
    ProjectRig rig;
    CHECK_THROWS_WITH(rig.run("project.open"), Catch::Matchers::ContainsSubstring("path"));
    CHECK_THROWS_WITH(rig.run("export.logic"), Catch::Matchers::ContainsSubstring("folder"));

    QVariantMap request;
    QObject::connect(&rig.registry, &Registry::fileDialogRequested, [&](const QVariantMap& asked) { request = asked; });
    rig.registry.activate("project.saveAs");
    CHECK(request.value("id") == "project.saveAs");
    CHECK(request.value("mode").toInt() == static_cast<int>(FileDialogMode::Save));
    CHECK(rig.controller->untitled());

    rig.registry.runWithFile("project.saveAs", QUrl::fromLocalFile(qs(rig.path("picked").string())));
    CHECK(rig.controller->folder() == rig.path("picked.erk"));
}

TEST_CASE("export for Logic writes numbered WAVs of one length and a MIDI file with tempo and named tracks") {
    ProjectRig rig;
    const std::string pad = rig.run("track.addAudio", {{"name", "Pad"}}).at("id");
    const std::string muted = rig.run("track.addAudio", {{"name", "Muted"}}).at("id");
    const std::string bass = rig.run("track.addAudio", {{"name", "Bass"}}).at("id");
    const std::string keys = rig.run("track.addMidi", {{"name", "Right 1"}, {"midiOut", {{"channel", 3}}}}).at("id");
    writeConstant(rig.path("ones.wav"), 48000, 1.0F);
    writeConstant(rig.path("half.wav"), 24000, 1.0F);
    rig.run("clip.import", {{"track", pad}, {"path", rig.path("ones.wav").string()}});
    rig.run("clip.import", {{"track", bass}, {"path", rig.path("half.wav").string()}, {"at", "2.1.0"}});
    rig.run("track.setMute", {{"track", muted}, {"value", true}});
    rig.run("project.setTempo", {{"bpm", 96}, {"numerator", 3}});
    rig.controller->project().editTrack(model::TrackId{keys}, "add notes", [](model::Track& track) {
        track.clips.emplace_back(
            model::MidiClip{.start = 960, .length = 960, .events = {{0, {0x90, 60, 100}}, {480, {0x80, 60, 0}}}});
    });

    const Json result = rig.run("export.logic", {{"folder", rig.path("out").string()}});
    const auto folder = rig.path("out") / "Untitled 1 for Logic";
    CHECK(result.at("folder") == folder.string());
    const std::vector<std::string> files = result.at("files");
    CHECK(files == std::vector<std::string>{(folder / "01 Pad.wav").string(), (folder / "03 Bass.wav").string(),
                                            (folder / "MIDI.mid").string()});
    const auto padInfo = readAudioInfo(folder / "01 Pad.wav");
    const auto bassInfo = readAudioInfo(folder / "03 Bass.wav");
    REQUIRE(padInfo);
    REQUIRE(bassInfo);
    CHECK(padInfo->frames == bassInfo->frames);
    CHECK(padInfo->frames == rig.controller->project().document().tempo.ticksToFrames(
                                 projectEnd(rig.controller->project().document()), kRate));
    CHECK(padInfo->sampleRate == kRate);

    const libremidi::reader midi = readMidiFile(folder / "MIDI.mid");
    CHECK(midi.format == 1);
    CHECK(midi.ticksPerBeat == Approx(core::kTicksPerQuarter));
    const libremidi::midi_bytes tempo{0xFF, std::to_underlying(libremidi::meta_event_type::TEMPO_CHANGE), 3, 0x09, 0x89,
                                      0x68};
    CHECK(std::ranges::contains(midi.tracks.at(0), tempo, [](const auto& event) { return event.m.bytes; }));
    const libremidi::midi_bytes meter{0xFF, 0x58, 4, 3, 2, 24, 8};
    CHECK(std::ranges::contains(midi.tracks.at(0), meter, [](const auto& event) { return event.m.bytes; }));
    REQUIRE(midi.tracks.size() == 2);
    CHECK(metaTexts(midi.tracks.at(0), libremidi::meta_event_type::TRACK_NAME) ==
          std::vector<std::string>{"Untitled 1"});
    CHECK(metaTexts(midi.tracks.at(1), libremidi::meta_event_type::TRACK_NAME) == std::vector<std::string>{"Right 1"});
    const auto& noteOn = midi.tracks.at(1).at(1);
    CHECK(noteOn.tick == 960);
    CHECK(noteOn.m.bytes == libremidi::midi_bytes{0x92, 60, 100});

    const Json listed = rig.run("export.logic", {{"folder", rig.path("listed").string()}, {"tracks", {"Muted"}}});
    CHECK(listed.at("files").size() == 2);

    rig.run("track.remove", {{"track", muted}});
    rig.run("export.logic", {{"folder", rig.path("out").string()}});
    CHECK(std::filesystem::exists(folder / "02 Bass.wav"));
    CHECK_FALSE(std::filesystem::exists(folder / "03 Bass.wav"));
}

TEST_CASE("remove unused audio deletes only files no clip uses after the agent confirms") {
    ProjectRig rig;
    const std::string id = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    writeConstant(rig.path("keep.wav"), 4800, 1.0F);
    writeConstant(rig.path("drop.wav"), 4800, 1.0F);
    rig.run("clip.import", {{"track", id}, {"path", rig.path("keep.wav").string()}});
    const Json dropped = rig.run("clip.import", {{"track", id}, {"path", rig.path("drop.wav").string()}});
    rig.run("clip.delete", {{"clip", dropped.at("id")}});

    CHECK_THROWS_WITH(rig.run("project.removeUnusedAudio"), Catch::Matchers::ContainsSubstring("confirm"));
    const Json result = rig.run("project.removeUnusedAudio", {{"confirm", true}});
    CHECK(result.at("removed") == Json::array({"drop.wav"}));
    CHECK(std::filesystem::exists(rig.controller->folder() / "audio" / "keep.wav"));
    CHECK_FALSE(std::filesystem::exists(rig.controller->folder() / "audio" / "drop.wav"));
    CHECK_FALSE(rig.controller->project().undo());
}

TEST_CASE("a project saves the timeline view and opens with it") {
    ProjectRig rig;
    const std::string id = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    rig.run("project.saveAs", {{"path", rig.path("Songs/View").string()}});
    rig.nav.setPixelsPerTick(0.2);
    rig.nav.setScrollTicks(3840);
    rig.nav.setTrackHeight(120);
    rig.run("automation.showLane", {{"track", id}, {"param", "pan"}});
    rig.run("project.save");
    CHECK(savedProject(rig.path("Songs/View.erk")).at("view").at("automationLanes").at(id) == Json::array({"pan"}));

    rig.run("project.new");
    rig.nav.setPixelsPerTick(0.05);
    rig.nav.setScrollTicks(0);
    rig.nav.setTrackHeight(60);
    rig.nav.setAutomationOpen(false);
    rig.nav.setAutomationLanes({});
    rig.run("project.open", {{"path", rig.path("Songs/View.erk").string()}});
    CHECK(rig.nav.pixelsPerTick() == Approx(0.2));
    CHECK(rig.nav.scrollTicks() == Approx(3840));
    CHECK(rig.nav.trackHeight() == 120);
    CHECK(rig.nav.automationOpen());
    CHECK(rig.nav.automationLane(QString::fromStdString(id)) == "pan");
    CHECK(rig.controller->project().document().view.empty());
}

TEST_CASE("a saved view keeps each track's lanes in order and skips lanes saved with the wrong type") {
    Nav saved;
    saved.showLane("vox", "pan", false);
    saved.showLane("vox", "mute", true);
    const Json state = viewState(saved);
    CHECK(state.at("automationLanes").at("vox") == Json::array({"pan", "mute"}));

    Nav opened;
    applyViewState(opened, state);
    CHECK(opened.shownLanes("vox") == QStringList{"pan", "mute"});
    applyViewState(opened, {{"automationLanes", {{"vox", "mute"}, {"keys", 3}}}});
    CHECK(opened.shownLanes("vox") == QStringList{"gain"});
    CHECK(opened.shownLanes("keys") == QStringList{"gain"});
}
