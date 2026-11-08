#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "emerick/model/json.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

const std::string kKeyboard1 = "Digital Keyboard Port 1";
const std::string kKeyboard2 = "Digital Keyboard Port 2";

// The user's song: 18 tracks on both keyboard ports, clock to port 1, and one audio track whose device we switch,
// which reopens the stream and so every MIDI port.
struct KeyboardSong {
    KeyboardSong() : rig({kKeyboard1, kKeyboard2}) {
        model::Document document;
        for (int index = 0; index < 18; ++index) {
            model::Track track;
            track.kind = "midi";
            const std::string& port = index % 2 == 0 ? kKeyboard1 : kKeyboard2;
            track.midiIn = {.portId = port, .channel = index % 16};
            track.midiOut = {.portId = port, .channel = index % 16};
            document.tracks.push_back(track);
        }
        model::Track audio;
        audio.kind = "audio";
        audio.audioOut = {"interface", {1, 2}};
        vox = audio.id.value;
        document.tracks.push_back(audio);
        document.clockPorts = {kKeyboard1};
        const auto folder = rig.path("song.erk");
        model::saveProject(document, folder);
        rig.run("project.open", {{"path", folder.string()}});
        rig.controller->engine().keepWarm(true);
    }

    void switchAudioDevice() const { rig.run("track.setAudioOut", {{"track", vox}, {"device", kMotuId}}); }

    // Locating waits for the engine, so a stalled transport leaves the playhead where it was.
    void checkTransportMoves() const {
        rig.run("transport.play");
        rig.run("transport.locate", {{"at", "3.1.0"}});
        const core::Ticks bar3 = 2 * rig.controller->ticksPerBar();
        CHECK(rig.controller->engine().position().ticks >= bar3);
        rig.run("transport.locate", {{"at", "1.1.0"}});
        CHECK(rig.controller->engine().position().ticks < rig.controller->ticksPerBar());
        CHECK_FALSE(projectState(*rig.controller).contains("sessionError"));
    }

    [[nodiscard]] bool listsInput(const std::string& port) const {
        return std::ranges::any_of(rig.controller->engine().midiPorts(),
                                   [&](const engine::MidiPortInfo& info) { return info.id == port && info.isInput; });
    }

    ProjectRig rig;
    std::string vox;
};

}  // namespace

TEST_CASE("unplugging a keyboard the app still lists while stopped keeps the transport moving") {
    KeyboardSong song;
    song.rig.midiLister->unplug(kKeyboard1, false);
    song.rig.midiLister->unplug(kKeyboard2, false);
    song.switchAudioDevice();

    song.checkTransportMoves();
    CHECK(processUntil([&] { return !song.listsInput(kKeyboard1) && !song.listsInput(kKeyboard2); }));
    const QString firstTrack =
        QString::fromStdString(song.rig.controller->project().document().tracks.front().id.value);
    CHECK(song.rig.controller->trackErrors().value(firstTrack).toString().contains("is missing, so the track plays"));
}

TEST_CASE("unplugging a keyboard while playing keeps the transport moving") {
    KeyboardSong song;
    song.rig.run("transport.play");
    song.rig.audio->process(kBlock);
    song.rig.midiLister->unplug(kKeyboard1, false);
    song.rig.midiLister->unplug(kKeyboard2, false);
    song.switchAudioDevice();

    song.checkTransportMoves();
}

TEST_CASE("a keyboard whose input alone goes away keeps its output and clock") {
    KeyboardSong song;
    song.rig.midiLister->unplugInput(kKeyboard1);
    song.switchAudioDevice();

    song.checkTransportMoves();
    CHECK(processUntil([&] { return !song.listsInput(kKeyboard1); }));
    CHECK(song.listsInput(kKeyboard2));
    CHECK(projectState(*song.rig.controller).at("routeErrors").size() == 9);
    song.rig.midi->clearSent();
    song.rig.run("transport.play");
    CHECK(processUntil([&] { return !song.rig.midi->sent(kKeyboard1).empty(); }));
}

TEST_CASE("a take recorded before the keyboard goes away is kept") {
    ProjectRig rig;
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    rig.run("track.setArm", {{"track", keys}, {"value", true}});
    rig.run("transport.record");
    rig.audio->process(kBlock);
    const auto now = [&] { return rig.audio->hostTimeAt(rig.audio->processedFrames()); };
    rig.midi->inject(kMidiPort, std::vector<std::uint8_t>{0x90, 60, 100}, now());
    rig.audio->process(24000);
    rig.midi->inject(kMidiPort, std::vector<std::uint8_t>{0x80, 60, 0}, now());
    rig.audio->process(24000);

    rig.midiLister->unplug(kMidiPort, true);
    processUntil([&] { return rig.controller->engine().midiPorts().empty(); });
    rig.run("transport.stop");

    REQUIRE(rig.waitForTakes(1) == 1);
    const Json clips = projectState(*rig.controller).at("tracks").at(0).at("clips");
    REQUIRE(clips.size() == 1);
    CHECK(clips.at(0).at("noteCount") == 1);
}
