#include <sndfile.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "emerick/app/core_actions.hpp"
#include "emerick/app/lua_host.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/app/stem_export.hpp"
#include "emerick/app/track_list_model.hpp"
#include "emerick/engine/sound_file.hpp"
#include "emerick/model/json.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;
using Catch::Approx;

namespace {

std::vector<float> readChannel(const std::filesystem::path& path, int channel) {
    SF_INFO info{};
    SNDFILE* file = engine::openSoundFile(path, SFM_READ, &info);
    REQUIRE(file != nullptr);
    std::vector<float> interleaved(static_cast<std::size_t>(info.frames * info.channels));
    sf_readf_float(file, interleaved.data(), info.frames);
    sf_close(file);
    std::vector<float> samples;
    for (std::size_t frame = 0; frame < static_cast<std::size_t>(info.frames); ++frame) {
        samples.push_back(
            interleaved[frame * static_cast<std::size_t>(info.channels) + static_cast<std::size_t>(channel)]);
    }
    return samples;
}

}  // namespace

TEST_CASE("track and clip actions round trip through undo and redo") {
    ProjectRig rig;
    const Json added = rig.run("track.addAudio", {{"name", "Vox"}});
    const std::string id = added.at("id");
    CHECK(added.at("audioIn").at("device") == "interface");
    CHECK(rig.nav.selectedTrack().toStdString() == id);

    CHECK(rig.run("track.rename", {{"track", "Vox"}, {"name", "Lead"}}).at("name") == "Lead");
    CHECK(rig.run("track.setMute", {{"track", id}, {"value", true}}).at("mute") == true);
    CHECK(rig.run("track.setGain", {{"track", "Lead"}, {"db", -6.0}}).at("gainDb") == -6.0);
    CHECK(rig.run("track.setPan", {{"track", "Lead"}, {"value", -0.5}}).at("pan") == -0.5);
    CHECK_THROWS_AS(rig.run("track.setPan", {{"track", id}, {"value", 1.5}}), AppError);
    CHECK_THROWS_AS(rig.run("track.setGain", {{"track", id}, {"db", 24.0}}), AppError);
    CHECK_THROWS_AS(rig.run("track.setMute", {{"track", "nobody"}, {"value", true}}), AppError);
    CHECK_THROWS_AS(rig.run("track.setAudioIn", {{"track", id}, {"device", "interface"}, {"channels", {3}}}), AppError);

    Json state = rig.run("project.undo");
    CHECK(state.at("tracks").at(0).at("pan") == 0.0);
    CHECK(state.at("redo") == "set track pan");
    state = rig.run("project.undo");
    CHECK(state.at("tracks").at(0).at("gainDb") == 0.0);
    CHECK(state.at("redo") == "set track gain");
    state = rig.run("project.undo");
    CHECK(state.at("tracks").at(0).at("mute") == false);
    state = rig.run("project.redo");
    CHECK(state.at("tracks").at(0).at("mute") == true);

    writeConstant(rig.path("ones.wav"), 48000, 1.0F);
    const Json imported =
        rig.run("clip.import", {{"track", id}, {"path", rig.path("ones.wav").string()}, {"at", "2.1.0"}});
    CHECK(imported.at("start") == 3840);
    CHECK(imported.at("startBbt") == "2.1.0");
    CHECK(imported.at("length") == 1920);
    CHECK(imported.at("lengthBbt") == "0.2.0");
    CHECK(std::filesystem::exists(rig.controller->folder() / imported.at("file").get<std::string>()));
    const std::string clip = imported.at("id");

    CHECK(rig.run("clip.move", {{"clip", clip}, {"start", 960}}).at("startBbt") == "1.2.0");
    const Json split = rig.run("clip.split", {{"clip", clip}, {"at", "1.3.0"}});
    CHECK(split.at("left").at("length") == 960);
    CHECK(split.at("right").at("start") == 1920);
    CHECK(split.at("right").at("sourceOffset") == 24000);
    const Json resized = rig.run("clip.resize", {{"clip", split.at("right").at("id")}, {"start", 2400}});
    CHECK(resized.at("length") == 480);
    CHECK(resized.at("sourceOffset") == 36000);
    CHECK(rig.run("clip.setFade", {{"clip", clip}, {"fadeIn", 100}}).at("fadeIn") == 100);
    CHECK(rig.run("clip.duplicate", {{"clip", clip}}).at("start") == 1920);
    CHECK(projectState(*rig.controller).at("tracks").at(0).at("clips").size() == 3);
    rig.run("project.undo");
    rig.run("project.undo");
    rig.run("project.undo");
    CHECK(projectState(*rig.controller).at("tracks").at(0).at("clips").size() == 2);
    rig.run("project.undo");
    CHECK(projectState(*rig.controller).at("tracks").at(0).at("clips").size() == 1);
    CHECK(rig.run("clip.delete", {{"clip", clip}}).at("tracks").at(0).at("clips").empty());

    CHECK(rig.run("project.setTempo", {{"bpm", 96}, {"numerator", 3}}).at("numerator") == 3);
    CHECK_THROWS_AS(rig.run("project.setTempo", {{"denominator", 5}}), AppError);
    CHECK(rig.run("transport.setClockOut", {{"port", kMidiPort}, {"enabled", true}}).at("clockOut").size() == 1);
    CHECK(rig.run("track.remove", {{"track", id}}).at("tracks").empty());

    const Json devices = rig.run("devices.list");
    CHECK(devices.at("audioInputs").at(2) ==
          Json{{"device", "interface"}, {"channels", {1, 2}}, {"label", "Interface 1-2"}});
    CHECK(devices.at("midiInputs").at(0).at("port") == kMidiPort);
}

TEST_CASE("track.setTrim rejects a trim outside 24 dB, undoes in one step and reaches the summary and the session") {
    ProjectRig rig;
    const std::string id = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    CHECK(rig.run("track.setTrim", {{"track", id}, {"db", 12.5}}).at("trimDb") == 12.5);
    CHECK_THROWS_AS(rig.run("track.setTrim", {{"track", id}, {"db", 24.5}}), AppError);
    CHECK_THROWS_AS(rig.run("track.setTrim", {{"track", id}, {"db", -30}}), AppError);
    CHECK(rig.run("project.summary").at("tracks").at(0).at("trimDb") == 12.5);
    CHECK(toSession(rig.controller->project().document(), rig.registry, "/projects/song").tracks[0].trim ==
          Approx(std::pow(10.0, 12.5 / 20.0)));

    const Json state = rig.run("project.undo");
    CHECK(state.at("tracks").at(0).at("trimDb") == 0.0);
    CHECK(state.at("redo") == "set track trim");
}

TEST_CASE("clip actions without a clip act on the selection and the selection drops removed clips") {
    ProjectRig rig;
    const std::string track = rig.run("track.addAudio").at("id");
    writeConstant(rig.path("ones.wav"), 48000, 1.0F);
    const std::string clip =
        rig.run("clip.import", {{"track", track}, {"path", rig.path("ones.wav").string()}}).at("id");
    CHECK_THROWS_AS(rig.run("clip.delete"), AppError);

    rig.nav.setSelectedTrack({});
    CHECK(rig.run("selection.set", {{"clips", {clip}}}).at("selectedClips") == Json{clip});
    CHECK(rig.nav.selectedTrack().toStdString() == track);
    CHECK_THROWS_AS(rig.run("selection.set", {{"clips", {"nobody"}}}), AppError);

    const Json copies = rig.run("clip.duplicate");
    const std::string copy = copies.at("clips").at(0).at("id");
    CHECK(copies.at("clips").at(0).at("start") == 1920);
    CHECK(rig.nav.selectedClips() == QStringList{QString::fromStdString(copy)});

    const Json splits = rig.run("clip.split", {{"at", 2880}});
    CHECK(splits.at("splits").at(0).at("left").at("length") == 960);
    CHECK_THROWS_AS(rig.run("clip.split", {{"at", 100}}), AppError);

    rig.run("clip.delete");
    CHECK(rig.nav.selectedClips().isEmpty());
    CHECK(projectState(*rig.controller).at("tracks").at(0).at("clips").size() == 2);
    rig.run("selection.set", {{"clips", {clip}}});
    rig.run("project.undo");
    rig.run("project.undo");
    rig.run("project.undo");
    rig.run("project.undo");
    CHECK(rig.nav.selectedClips().isEmpty());
}

TEST_CASE("snapping rounds to the nearest beat until snap is toggled off") {
    ProjectRig rig;
    CHECK(rig.controller->snapped(1000) == 960);
    CHECK(rig.controller->snapped(1500) == 1920);
    CHECK(rig.controller->snapped(-300) == 0);
    CHECK(rig.run("edit.toggleSnap").at("snap") == false);
    CHECK(rig.controller->snapped(1000) == 1000);
    rig.run("edit.toggleSnap");
    rig.run("project.setTempo", {{"denominator", 8}});
    CHECK(rig.controller->snapped(1000) == 960);
    CHECK(rig.controller->snapped(800) == 960);
    CHECK(rig.controller->snapped(200) == 0);
}

TEST_CASE("holding Ctrl places a drag the other way from the snap setting") {
    ProjectRig rig;
    CHECK(rig.controller->placed(1000, Qt::NoModifier) == 960);
    CHECK(rig.controller->placed(1000, Qt::ControlModifier) == 1000);
    CHECK(rig.controller->placed(1000, Qt::AltModifier) == 960);
    rig.run("edit.toggleSnap");
    CHECK(rig.controller->placed(1000, Qt::NoModifier) == 1000);
    CHECK(rig.controller->placed(1000, Qt::ControlModifier | Qt::ShiftModifier) == 960);
    CHECK(rig.controller->placed(-5, Qt::NoModifier) == 0);
}

TEST_CASE("the session mapping turns the document into engine tracks") {
    ProjectRig rig;
    model::Document document;
    model::Track audio;
    audio.name = "Pad";
    audio.gainDb = -20.0;
    audio.pan = 0.25;
    audio.audioOut = {"interface", {1, 2}};
    model::AudioClip clip;
    clip.file = "audio/pad.wav";
    clip.length = 960;
    clip.gainDb = 20.0;
    audio.clips.emplace_back(clip);
    model::Track midi;
    midi.kind = "midi";
    midi.audioIn = {"interface", {1}};
    midi.midiIn = {kMidiPort, 2, false, true, {}};
    midi.midiOut = {kMidiPort, 3, true, false, {}};
    model::MidiClip notes;
    notes.start = 480;
    notes.length = 960;
    notes.events = {{0, {0x90, 60, 100}}, {959, {0x80, 60, 0}}, {960, {0x90, 62, 100}}};
    midi.clips.emplace_back(notes);
    model::Track unknown;
    unknown.kind = "nobody.knows";
    document.tracks = {audio, midi, unknown};
    document.clockPorts = {kMidiPort};

    const engine::Session session = toSession(document, rig.registry, "/projects/song");
    REQUIRE(session.tracks.size() == 2);
    CHECK(session.takeFolder == "/projects/song/audio");
    CHECK(session.clockPorts == std::vector{kMidiPort});
    const engine::Track& pad = session.tracks[0];
    CHECK(pad.id == audio.id.value);
    CHECK(pad.kind == engine::TrackKind::audio);
    CHECK(pad.gain == Approx(0.1F));
    CHECK(pad.pan == 0.25F);
    CHECK(session.panLaw == engine::PanLaw::compensated);
    rig.registry.setSetting(kPanLawSetting, "-6 dB linear");
    CHECK(toSession(document, rig.registry, "/projects/song").panLaw == engine::PanLaw::linear);
    CHECK(pad.audioClips.at(0).path == "/projects/song/audio/pad.wav");
    CHECK(pad.audioClips.at(0).gain == Approx(10.0F));
    const engine::Track& keys = session.tracks[1];
    CHECK(keys.kind == engine::TrackKind::midi);
    CHECK(keys.audioIn.deviceId.empty());
    CHECK(keys.midiIn.channel == 2);
    CHECK(keys.claimsSysex);
    CHECK(keys.midiThru);
    CHECK(keys.midiClips.at(0).start == 480);
    CHECK(keys.midiClips.at(0).events.size() == 2);
}

TEST_CASE("save then open restores everything") {
    ProjectRig rig;
    const std::string id = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    rig.run("track.addMidi", {{"name", "Keys"}, {"midiIn", {{"port", kMidiPort}, {"channel", 4}, {"sysex", true}}}});
    rig.run("project.setTempo", {{"bpm", 96}});
    writeConstant(rig.path("ones.wav"), 4800, 1.0F);
    rig.run("clip.import", {{"track", id}, {"path", rig.path("ones.wav").string()}});

    const auto folder = rig.path("song.erk");
    const Json saved = rig.run("project.saveAs", {{"path", folder.string()}});
    CHECK(saved.at("untitled") == false);
    CHECK(std::filesystem::exists(folder / "audio" / "ones.wav"));
    const Json before = model::toJson(rig.controller->project().document());

    CHECK(rig.run("project.new").at("tracks").empty());
    const Json opened = rig.run("project.open", {{"path", folder.string()}});
    CHECK(model::toJson(rig.controller->project().document()) == before);
    CHECK(opened.at("path") == folder.string());
    CHECK(opened.at("tracks").at(1).at("midiIn").at("channel") == 4);
    CHECK(opened.at("tempo").at("bpm") == 96.0);
    CHECK_FALSE(opened.contains("sessionError"));
    CHECK_THROWS_AS(rig.run("project.open", {{"path", rig.path("missing").string()}}), AppError);
}

TEST_CASE("opening a project repairs a device id with text glued on and skips only a track whose device is gone") {
    ProjectRig rig;
    model::Document document;
    model::Track glued;
    glued.kind = "audio";
    glued.audioIn = {"interface:1,2", {1, 2}};
    model::Track gone;
    gone.kind = "audio";
    gone.audioOut = {"unplugged-device", {1, 2}};
    document.tracks = {glued, gone};
    const auto folder = rig.path("old.erk");
    model::saveProject(document, folder);

    const Json opened = rig.run("project.open", {{"path", folder.string()}});
    CHECK(opened.at("tracks").at(0).at("audioIn").at("device") == "interface");
    CHECK(opened.at("tracks").at(1).at("audioOut").at("device") == "unplugged-device");
    CHECK_FALSE(opened.contains("sessionError"));
    REQUIRE(opened.at("routeErrors").size() == 1);
    CHECK(opened.at("routeErrors").at(0).at("track") == gone.id.value);
}

TEST_CASE("every shortcut has one owner and the menu bar follows the Logic order") {
    ProjectRig rig;
    registerCoreActions(rig.registry, rig.nav, {});
    std::map<std::string, std::string> owners;
    for (const Action& action : rig.registry.actions()) {
        std::vector<std::string> keys = action.extraShortcuts;
        if (!action.shortcut.empty()) {
            keys.push_back(action.shortcut);
        }
        for (const std::string& key : keys) {
            CAPTURE(key, action.id);
            CHECK(owners.emplace(key, action.id).second);
        }
    }
    CHECK(rig.registry.menuBar() == QStringList{"File", "Edit", "Track", "Transport", "View", "Settings", "Help"});
}

TEST_CASE("track actions without a track param act on the selected track") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    CHECK(rig.run("track.setMute").at("mute") == true);
    CHECK(rig.run("track.setMute").at("mute") == false);
    const Json copy = rig.run("track.duplicate");
    CHECK(copy.at("name") == "Audio 1 copy");
    CHECK(rig.controller->project().document().tracks.size() == 2);
    CHECK(rig.controller->project().undoName().has_value());
    rig.run("track.select", {{"track", vox}});
    rig.run("track.remove");
    CHECK(rig.controller->project().track(model::TrackId{vox}) == nullptr);
    rig.run("project.undo");
    CHECK(rig.controller->project().track(model::TrackId{vox}) != nullptr);
}

TEST_CASE("an action that edits and then fails leaves the document and the undo history as they were") {
    ProjectRig rig;
    rig.run("track.addAudio", {{"name", "Vox"}});
    const model::Document before = rig.controller->project().document();
    rig.registry.addAction({.id = "test.editThenFail", .run = [&rig](const Json&) -> Json {
                                rig.run("track.addMidi");
                                rig.run("track.rename", {{"track", "Vox"}, {"name", "Lead"}});
                                throw AppError("failed after two edits");
                            }});

    CHECK_THROWS_AS(rig.run("test.editThenFail"), AppError);
    CHECK(rig.controller->project().document() == before);
    CHECK(rig.controller->project().undoName() == "add track Vox");
}

TEST_CASE("a tempo of NaN is refused") {
    ProjectRig rig;
    const core::TempoMap before = rig.controller->project().document().tempo;
    CHECK_THROWS_AS(rig.run("project.setTempo", {{"bpm", std::nan("")}}), AppError);
    CHECK(rig.controller->project().document().tempo == before);
}

TEST_CASE("a path the file system refuses fails the action with an AppError") {
    ProjectRig rig;
    const std::string tooLong = rig.path(std::string(5000, 'x')).string();
    CHECK_THROWS_AS(rig.run("project.saveAs", {{"path", tooLong}}), AppError);
    CHECK_THROWS_AS(rig.run("export.stems", {{"folder", tooLong}}), AppError);
}

TEST_CASE("copy and paste put clips at the playhead on the selected track, and bar keys step on bar lines") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    writeConstant(rig.path("ones.wav"), 4800, 1.0F);
    const std::string clip = rig.run("clip.import", {{"track", vox}, {"path", rig.path("ones.wav").string()}}).at("id");
    rig.run("selection.set", {{"clips", {clip}}});
    rig.run("clip.copy");
    const core::Ticks bar = rig.controller->ticksPerBar();
    rig.run("transport.locate", {{"at", bar + bar / 2}});
    rig.run("transport.barBack");
    CHECK(rig.controller->positionTicks() == bar);
    const Json pasted = rig.run("clip.paste");
    CHECK(pasted.at("clips").at(0).at("start") == bar);
    CHECK(rig.controller->project().track(model::TrackId{vox})->clips.size() == 2);
    const std::string keys = rig.run("track.addMidi").at("id");
    CHECK_THROWS_AS(rig.run("clip.paste", {{"track", keys}}), AppError);
}

TEST_CASE("zoom keeps the playhead in place while it shows, else the middle of the lane, within the zoom limits") {
    ProjectRig rig;
    rig.nav.setLaneWidth(1000);
    rig.nav.setScrollTicks(0);
    const auto screenX = [&](double ticks) { return (ticks - rig.nav.scrollTicks()) * rig.nav.pixelsPerTick(); };

    rig.run("transport.locate", {{"at", 4000}});
    const double playheadX = screenX(4000);
    REQUIRE(playheadX < 1000);
    const double before = rig.nav.pixelsPerTick();
    rig.run("view.zoomIn");
    CHECK(rig.nav.pixelsPerTick() == Approx(before * std::numbers::sqrt2));
    CHECK(screenX(4000) == Approx(playheadX));
    rig.run("view.zoomOut");
    CHECK(rig.nav.pixelsPerTick() == Approx(before));
    CHECK(screenX(4000) == Approx(playheadX));

    rig.run("transport.locate", {{"at", 400000}});
    rig.nav.setScrollTicks(0);
    const double middle = 500 / rig.nav.pixelsPerTick();
    rig.run("view.zoomIn");
    CHECK(screenX(middle) == Approx(500));

    for (int step = 0; step < 40; ++step) {
        rig.run("view.zoomOut");
    }
    CHECK(rig.nav.pixelsPerTick() == kMinPixelsPerTick);
}

TEST_CASE("export stems renders each audio track from bar 1 to the project end") {
    ProjectRig rig;
    rig.registry.setSetting(kPanLawSetting, "-3 dB constant power");
    const std::string pad = rig.run("track.addAudio", {{"name", "Pad"}}).at("id");
    const std::string bass = rig.run("track.addAudio", {{"name", "Bass"}}).at("id");
    writeConstant(rig.path("ones.wav"), 48000, 1.0F);
    writeConstant(rig.path("half.wav"), 24000, 1.0F);
    rig.run("clip.import", {{"track", pad}, {"path", rig.path("ones.wav").string()}});
    rig.run("clip.import", {{"track", bass}, {"path", rig.path("half.wav").string()}, {"at", "2.1.0"}});
    rig.run("track.setGain", {{"track", pad}, {"db", -6.0206}});
    rig.run("track.setMute", {{"track", bass}, {"value", true}});
    rig.run("track.setPan", {{"track", pad}, {"value", 1.0}});

    const auto folder = rig.path("stems");
    const Json result = rig.run("export.stems", {{"folder", folder.string()}});
    REQUIRE(result.at("files").size() == 2);
    CHECK(result.at("end").at("seconds") == 2.5);
    for (const char* name : {"Pad.wav", "Bass.wav"}) {
        const auto info = readAudioInfo(folder / name);
        REQUIRE(info);
        CHECK(info->frames == 120000);
        CHECK(info->channels == 2);
    }
    const auto padSamples = readChannel(folder / "Pad.wav", 1);
    CHECK(padSamples[24000] == Approx(0.5F).margin(1e-4));
    CHECK(padSamples[60000] == 0.0F);
    CHECK(readChannel(folder / "Pad.wav", 0)[24000] == Approx(0.0F).margin(1e-6));
    CHECK(readChannel(folder / "Bass.wav", 0)[100000] == Approx(std::numbers::sqrt2_v<float> / 2));

    rig.run("export.mix", {{"path", (folder / "mix.wav").string()}});
    const auto mix = readChannel(folder / "mix.wav", 1);
    CHECK(mix[24000] == Approx(0.5F).margin(1e-4));
    CHECK(mix[100000] == 0.0F);
    CHECK(readChannel(folder / "mix.wav", 0)[24000] == Approx(0.0F).margin(1e-6));

    CHECK(rig.run("export.stems", {{"folder", rig.path("one").string()}, {"tracks", {"Bass"}}}).at("files").size() ==
          1);
}

TEST_CASE("export mix plays bus returns, and stems export each track dry and each bus with the tracks feeding it") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    const std::string verb = rig.run("track.addBus", {{"name", "Verb"}}).at("id");
    writeConstant(rig.path("ones.wav"), 48000, 1.0F);
    rig.run("clip.import", {{"track", vox}, {"path", rig.path("ones.wav").string()}});
    rig.run("send.add", {{"track", vox}, {"bus", verb}, {"db", -6.0206}});

    const auto folder = rig.path("export");
    rig.run("export.mix", {{"path", (folder / "mix.wav").string()}});
    CHECK(readChannel(folder / "mix.wav", 0)[24000] == Approx(1.5F).margin(1e-3));

    CHECK(rig.run("export.stems", {{"folder", folder.string()}}).at("files").size() == 2);
    CHECK(readChannel(folder / "Vox.wav", 0)[24000] == Approx(1.0F).margin(1e-3));
    CHECK(readChannel(folder / "Verb.wav", 0)[24000] == Approx(0.5F).margin(1e-3));
}

TEST_CASE("stems from one render match each track and bus rendered on its own to the sample") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    const std::string bass = rig.run("track.addAudio", {{"name", "Bass"}}).at("id");
    const std::string verb = rig.run("track.addBus", {{"name", "Verb"}}).at("id");
    const std::string group = rig.run("track.addBus", {{"name", "Group"}}).at("id");
    writeConstant(rig.path("ones.wav"), 48000, 0.5F);
    writeConstant(rig.path("half.wav"), 24000, 0.25F);
    rig.run("clip.import", {{"track", vox}, {"path", rig.path("ones.wav").string()}});
    rig.run("clip.import", {{"track", bass}, {"path", rig.path("half.wav").string()}, {"at", "2.1.0"}});
    rig.run("insert.add", {{"track", verb}, {"type", "reverb"}});
    rig.run("send.add", {{"track", vox}, {"bus", verb}, {"db", -6.0}});
    rig.run("track.setPan", {{"track", vox}, {"value", 0.5}});
    rig.run("track.setOutputBus", {{"track", bass}, {"bus", group}});
    rig.run("track.setMute", {{"track", bass}, {"value", true}});

    const auto folder = rig.path("stems");
    REQUIRE(rig.run("export.stems", {{"folder", folder.string()}}).at("files").size() == 4);
    const model::Document& document = rig.controller->project().document();
    const engine::Session session = toSession(document, rig.registry, rig.controller->folder());
    const PlaybackContext context{.tempo = document.tempo, .folder = rig.controller->folder(), .document = &document};
    const engine::Frames frames = document.tempo.ticksToFrames(exportEnd(session, document), kRate);
    for (const auto& [id, name] : std::map<std::string, std::string>{
             {vox, "Vox.wav"}, {bass, "Bass.wav"}, {verb, "Verb.wav"}, {group, "Group.wav"}}) {
        INFO(name);
        std::vector<std::vector<float>> alone(2);
        const std::unique_ptr<OfflineRender> render = trackRender(session, rig.registry, context, id, kRate);
        render->process(
            frames, [&alone](const std::vector<std::vector<float>>& chunk) { appendStereo(alone, chunk); },
            [&render] { render->idleInstruments(); });
        CHECK(readChannel(folder / name, 0) == alone[0]);
        CHECK(readChannel(folder / name, 1) == alone[1]);
    }
}

TEST_CASE("a plugin track kind renders to MIDI and folds takes into its data") {
    ProjectRig rig;
    rig.registry.addTrackType(
        {.kind = "test.chord",
         .title = "Chord",
         .toEngine =
             [](const model::Track& track, const PlaybackContext& context) {
                 engine::Track playback = midiEngineTrack(track);
                 playback.midiOut = {kMidiPort, 5};
                 playback.midiClips = {
                     {.start = context.tempo.ticksPerBar(),
                      .events = {{0, {0x90, static_cast<std::uint8_t>(track.data.value("root", 60)), 100}}}}};
                 return playback;
             },
         .fromTake = [](model::Track& track, const engine::RecordedTake& take,
                        const TakeContext&) { track.data["recorded"] = take.midiEvents.size(); }});
    model::Track chord;
    chord.kind = "test.chord";
    chord.name = "Chords";
    chord.midiIn = {kMidiPort, 0, false, false, {}};
    chord.data = {{"root", 62}};
    rig.controller->project().addTrack(chord);

    const engine::Session session =
        toSession(rig.controller->project().document(), rig.registry, rig.controller->folder());
    REQUIRE(session.tracks.size() == 1);
    CHECK(session.tracks[0].kind == engine::TrackKind::midi);
    CHECK(session.tracks[0].midiOut.portId == kMidiPort);
    CHECK(session.tracks[0].midiOut.channel == 5);
    CHECK(session.tracks[0].midiClips.at(0).start == 3840);
    CHECK(session.tracks[0].midiClips.at(0).events.at(0).bytes == std::vector<std::uint8_t>{0x90, 62, 100});

    rig.run("track.setArm", {{"track", "Chords"}, {"value", true}});
    rig.run("transport.record");
    rig.audio->process(kBlock);
    const std::vector<std::uint8_t> note{0x90, 64, 90};
    rig.midi->inject(kMidiPort, note, rig.audio->hostTimeAt(rig.audio->processedFrames()));
    rig.audio->process(kBlock * 4);
    rig.run("transport.stop");
    REQUIRE(rig.waitForTakes(1) == 1);
    const Json track = projectState(*rig.controller).at("tracks").at(0);
    CHECK(track.at("data").at("recorded") == 1);
    CHECK(track.at("clips").empty());
}

TEST_CASE("the Lua one-liner mutes every track") {
    ProjectRig rig;
    rig.run("track.addAudio");
    rig.run("track.addMidi");
    const UiApi ui{rig.registry};
    LuaHost lua{rig.registry, ui};
    const Json result = lua.eval(
        R"(for _, t in ipairs(emerick.project().tracks) do emerick.run("track.setMute", {track=t.id, value=true}) end)");
    CHECK(result.at("ok") == true);
    for (const Json& track : projectState(*rig.controller).at("tracks")) {
        CHECK(track.at("mute") == true);
    }
}

TEST_CASE("the track list model follows the document") {
    ProjectRig rig;
    TrackListModel model{*rig.controller, rig.registry};
    const std::string id = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    REQUIRE(model.rowCount() == 1);
    const QModelIndex row = model.index(0);
    CHECK(model.data(row, static_cast<int>(TrackListModel::Role::name)).toString() == "Vox");
    rig.run("track.setArm", {{"track", id}, {"value", true}});
    CHECK(model.data(row, static_cast<int>(TrackListModel::Role::armed)).toBool());
    writeConstant(rig.path("ones.wav"), 4800, 1.0F);
    rig.run("clip.import", {{"track", id}, {"path", rig.path("ones.wav").string()}});
    const QVariantList clips = model.data(row, static_cast<int>(TrackListModel::Role::clips)).toList();
    REQUIRE(clips.size() == 1);
    CHECK(clips.front().toMap().value("length").toLongLong() == 192);
    CHECK(model.roleNames().value(static_cast<int>(TrackListModel::Role::clips)) == "clips");
    CHECK(model.data(row, static_cast<int>(TrackListModel::Role::audioRoutes)).toBool());
    CHECK_FALSE(model.data(row, static_cast<int>(TrackListModel::Role::midiRoutes)).toBool());
}

TEST_CASE("route actions merge the given fields into the current route") {
    ProjectRig rig;
    const std::string keys =
        rig.run("track.addMidi", {{"midiIn", {{"port", kMidiPort}, {"channel", 4}, {"sysex", true}}}}).at("id");
    Json route = rig.run("track.setMidiIn", {{"track", keys}, {"channel", 9}}).at("midiIn");
    CHECK(route == Json{{"port", kMidiPort}, {"channel", 9}, {"thru", false}, {"sysex", true}});
    route = rig.run("track.setMidiIn", {{"track", keys}, {"port", ""}}).at("midiIn");
    CHECK(route.at("port") == "");
    CHECK(route.at("channel") == 9);
    CHECK(rig.run("track.setMidiOut", {{"track", keys}, {"port", kMidiPort}}).at("midiOut").at("channel") == 0);
    CHECK_THROWS_AS(rig.run("track.setMidiOut", {{"track", keys}, {"channel", 17}}), AppError);

    const std::string vox = rig.run("track.addAudio").at("id");
    route = rig.run("track.setAudioIn", {{"track", vox}, {"channels", {2}}}).at("audioIn");
    CHECK(route == Json{{"device", "interface"}, {"channels", {2}}, {"text", "Interface 2"}});
    CHECK(rig.run("track.setAudioIn", {{"track", vox}, {"device", "interface"}}).at("audioIn").at("channels") ==
          Json::array({2}));
    CHECK(rig.run("track.setAudioOut", {{"track", vox}, {"device", ""}}).at("audioOut").at("channels").empty());
    CHECK(rig.run("track.setAudioOut", {{"track", vox}, {"device", "interface"}}).at("audioOut").at("channels") ==
          Json{1, 2});
}

TEST_CASE("project state names a device by its name and channels, never by its UID") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const Json route =
        rig.run("track.setAudioOut", {{"track", vox}, {"device", kMotuId}, {"channels", {3, 4}}}).at("audioOut");
    CHECK(route.at("device") == kMotuId);
    CHECK(route.at("text") == "MOTU M2 3-4");
    CHECK(projectState(*rig.controller).at("tracks").at(0).at("audioOut").at("text") == "MOTU M2 3-4");
    try {
        rig.run("track.setAudioOut", {{"track", vox}, {"channels", {9}}});
        FAIL("channel 9 is past the device");
    } catch (const AppError& error) {
        CHECK(std::string{error.what()}.starts_with("channels for MOTU M2"));
    }
}

TEST_CASE("the route pickers list devices by name and channels apart from them") {
    ProjectRig rig;
    const QVariantMap devices = rig.controller->routeDevices();
    const QVariantMap none = devices.value("midiIn").toList().front().toMap();
    CHECK(none.value("name").toString() == "None");
    CHECK(none.value("params").toMap() == QVariantMap{{"port", ""}});
    const QVariantList audioIn = devices.value("audioIn").toList();
    REQUIRE(audioIn.size() == 4);
    CHECK(audioIn.back().toMap().value("name").toString() == "MOTU M2");
    const QVariantMap interface = audioIn.at(1).toMap();
    CHECK(interface.value("name").toString() == "Interface");
    CHECK(interface.value("params").toMap() == QVariantMap{{"device", "interface"}});
    QStringList channels;
    for (const QVariant& option : interface.value("channels").toList()) {
        channels.append(option.toMap().value("name").toString());
    }
    CHECK(channels == QStringList{"1", "2", "1-2"});
    CHECK(devices.value("midiChannels").toList().size() == kMidiChannels + 1);
}

TEST_CASE("arm all tracks arms every track in one undo step and disarms them when all are armed") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const std::string keys = rig.run("track.addMidi").at("id");
    rig.run("track.setArm", {{"track", vox}, {"value", true}});
    const auto armed = [&] {
        return std::ranges::count_if(rig.controller->project().document().tracks, &model::Track::armed);
    };
    rig.run("track.armAll");
    CHECK(armed() == 2);
    rig.run("track.armAll");
    CHECK(armed() == 0);
    rig.run("project.undo");
    CHECK(armed() == 2);
    rig.run("project.undo");
    CHECK(armed() == 1);
    rig.run("track.armAll", {{"value", false}});
    CHECK(armed() == 0);
    (void)keys;
}

TEST_CASE("Cmd-click and Shift-click select several tracks, and edits with selection set them all in one undo step") {
    ProjectRig rig;
    const std::string first = rig.run("track.addAudio").at("id");
    const std::string second = rig.run("track.addAudio").at("id");
    const std::string third = rig.run("track.addAudio").at("id");
    const auto id = [](const std::string& text) { return QString::fromStdString(text); };
    const auto track = [&](const std::string& text) { return *rig.controller->project().track(model::TrackId{text}); };

    rig.run("track.select", {{"track", first}});
    rig.run("track.select", {{"track", third}, {"range", true}});
    CHECK(rig.nav.selectedTracks() == QStringList{id(first), id(second), id(third)});
    CHECK(rig.nav.selectedTrack() == id(third));
    rig.run("track.select", {{"track", second}, {"extend", true}});
    CHECK(rig.nav.selectedTracks() == QStringList{id(first), id(third)});

    rig.run("track.setMute", {{"track", first}, {"value", true}, {"selection", true}});
    CHECK(track(first).mute);
    CHECK_FALSE(track(second).mute);
    CHECK(track(third).mute);
    rig.run("project.undo");
    CHECK_FALSE(track(first).mute);
    CHECK_FALSE(track(third).mute);

    rig.run("track.setGain", {{"track", third}, {"db", -6.0}});
    rig.run("track.setGain", {{"track", first}, {"db", 3.0}, {"selection", true}});
    CHECK(track(first).gainDb == Approx(3.0));
    CHECK(track(third).gainDb == Approx(-3.0));
    rig.run("track.setColor", {{"track", third}, {"color", 4}, {"selection", true}});
    CHECK(track(first).color == 4);
    CHECK(track(second).color != 4);

    rig.run("track.setMute", {{"track", second}, {"value", true}, {"selection", true}});
    CHECK_FALSE(track(first).mute);
    CHECK(track(second).mute);

    rig.run("track.selectAll");
    CHECK(rig.nav.selectedTracks().size() == 3);
    rig.run("track.remove", {{"track", first}, {"selection", true}});
    CHECK(rig.controller->project().document().tracks.empty());
    rig.run("project.undo");
    CHECK(rig.controller->project().document().tracks.size() == 3);
}

TEST_CASE("the arrow keys step the playhead by one bar and Shift steps eight, and a tempo change keeps its bar") {
    ProjectRig rig;
    CHECK(rig.registry.action("transport.barForward")->shortcut == "Right");
    CHECK(rig.registry.action("transport.eightBarsBack")->shortcut == "Shift+Left");
    CHECK(rig.registry.action("clip.selectNext")->shortcut.empty());
    const core::Ticks bar = rig.controller->ticksPerBar();
    rig.run("transport.barForward");
    rig.run("transport.eightBarsForward");
    CHECK(rig.controller->positionTicks() == 9 * bar);
    rig.run("project.setTempo", {{"bpm", 60}});
    CHECK(processUntil([&] { return rig.controller->positionTicks() == 9 * bar; }));
}

TEST_CASE("renaming clips names them in one undo step, and an empty name clears it") {
    ProjectRig rig;
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    const std::string first = rig.run("midi.addClip", {{"track", keys}, {"at", 0}}).at("id");
    const std::string second = rig.run("midi.addClip", {{"track", keys}, {"at", 3840}}).at("id");
    const auto nameOf = [&](const std::string& id) {
        return model::clipName(*rig.controller->project().findClip(model::ClipId{id})->clip);
    };

    const Json renamed = rig.run("clip.rename", {{"clip", first}, {"name", "Intro"}});
    CHECK(renamed.at("clips").at(0).at("name") == "Intro");
    CHECK(nameOf(first) == "Intro");
    CHECK(rig.controller->project().undoName() == "rename clip to Intro");
    CHECK(projectState(*rig.controller).at("tracks").at(0).at("clips").at(0).at("name") == "Intro");

    rig.nav.setSelectedClips({QString::fromStdString(first), QString::fromStdString(second)});
    rig.run("clip.rename", {{"name", "Verse"}});
    CHECK(nameOf(first) == "Verse");
    CHECK(nameOf(second) == "Verse");
    rig.run("project.undo");
    CHECK(nameOf(first) == "Intro");
    CHECK(nameOf(second).empty());

    rig.run("clip.rename", {{"clip", first}, {"name", ""}});
    CHECK(nameOf(first).empty());
    CHECK_THROWS_AS(rig.run("clip.rename", {{"clip", first}}), AppError);
}
