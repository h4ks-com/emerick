#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "emerick/core/midi.hpp"
#include "emerick/yamaha/chart.hpp"
#include "yamaha_rig.hpp"

using namespace emerick;
using namespace emerick::yamaha;
using namespace emerick::yamaha::test;
using app::test::kBlock;
using app::test::kMidiPort;

namespace {

const std::string kKeyboard = "Digital Keyboard Port 1";
const std::string kController = "Digital Keyboard Port 2";

std::vector<Bytes> sentTo(const YamahaRig& rig, const std::string& port) {
    std::vector<Bytes> result;
    for (const auto& sent : rig.midi->sent(port)) {
        result.push_back(sent.bytes);
    }
    return result;
}

bool sentVoices(const YamahaRig& rig) { return std::ranges::contains(sentTo(rig, kMidiPort), Bytes{0xB8, 0, 127}); }

}  // namespace

TEST_CASE("setting up the arranger adds Chords, Style and one track per part in one undo step") {
    YamahaRig rig;
    rig.run("track.addAudio", {{"name", "Vox"}});
    const Json state = rig.run("yamaha.setupArranger");
    const Json tracks = state.at("tracks");
    REQUIRE(tracks.size() == 19);
    CHECK(tracks.at(1).at("kind") == "yamaha.chord");
    CHECK(tracks.at(1).at("name") == "Chords");
    CHECK(tracks.at(1).at("midiIn") == Json{{"port", kMidiPort}, {"channel", 0}, {"thru", false}, {"sysex", true}});
    CHECK(tracks.at(1).at("midiOut") == Json{{"port", kMidiPort}, {"channel", 0}, {"thru", false}, {"sysex", false}});
    CHECK(tracks.at(2).at("kind") == "yamaha.style");
    CHECK(tracks.at(2).at("midiOut") == Json{{"port", kMidiPort}, {"channel", 16}, {"thru", false}, {"sysex", false}});
    for (int channel = 1; channel <= 16; ++channel) {
        const Json& part = tracks.at(static_cast<std::size_t>(channel) + 2);
        const Json route{{"port", kMidiPort}, {"channel", channel}, {"thru", false}, {"sysex", false}};
        CHECK(part.at("kind") == "midi");
        CHECK(part.at("midiIn") == route);
        CHECK(part.at("midiOut") == route);
    }
    CHECK(tracks.at(3).at("name") == "Right 1");
    CHECK(tracks.at(4).at("name") == "Right 2");
    CHECK(tracks.at(6).at("name") == "Left");
    CHECK(tracks.at(3).at("icon") == "piano-keys");
    CHECK(tracks.at(13).at("name") == "Bass");
    CHECK(tracks.at(0).at("armed") == false);
    for (std::size_t index = 1; index < tracks.size(); ++index) {
        CHECK(tracks.at(index).at("armed") == true);
    }
    CHECK(state.at("clockOut") == Json::array({kMidiPort}));

    rig.run("project.undo");
    CHECK(rig.controller->project().document().tracks.size() == 1);
    CHECK(rig.controller->project().document().clockPorts.empty());
    rig.run("project.redo");
    CHECK(rig.controller->project().document().tracks.size() == 19);
}

TEST_CASE("the arranger records and presses Style buttons on Port 1 and plays the parts on Port 2") {
    YamahaRig rig({kController, "Other", kKeyboard});
    rig.run("yamaha.setupArranger");
    const auto& document = rig.controller->project().document();
    CHECK(std::ranges::all_of(document.tracks, [](const model::Track& track) {
        return track.midiIn.portId == kKeyboard &&
               track.midiOut.portId == (track.kind == "midi" ? kController : kKeyboard);
    }));
    CHECK(document.tracks.at(1).midiOut.channel == 16);
    CHECK(document.clockPorts == std::vector<std::string>{kKeyboard});
    CHECK_THROWS_AS(rig.run("yamaha.setupArranger", {{"port", "Missing"}}), app::AppError);

    rig.run("yamaha.setupArranger", {{"port", kController}});
    CHECK(document.tracks.at(19).midiOut.portId == kController);
    CHECK(document.tracks.at(21).midiOut.portId == kKeyboard);
    CHECK(document.clockPorts == std::vector<std::string>{kKeyboard, kController});

    rig.registry.setSetting("yamaha.controllerPort", "Other");
    rig.run("yamaha.setupArranger");
    CHECK(document.tracks.at(37).midiOut.portId == "Other");
}

TEST_CASE("the arranger adds a stereo audio track on the keyboard's audio input when it runs at the project rate") {
    YamahaRig rig({kMidiPort}, app::test::kKeyboardRate);
    const Json tracks = rig.run("yamaha.setupArranger").at("tracks");
    REQUIRE(tracks.size() == 19);
    CHECK(tracks.back().at("kind") == "audio");
    CHECK(tracks.back().at("name") == "Keyboard Audio");
    CHECK(tracks.back().at("audioIn").at("device") == "keyboard");
    CHECK(tracks.back().at("audioIn").at("channels") == Json{1, 2});
    CHECK(tracks.back().at("armed") == true);
}

TEST_CASE("playback sends each part's notes on its channel and never echoes the keyboard back") {
    YamahaRig rig;
    rig.run("yamaha.setupArranger");
    const auto& tracks = rig.controller->project().document().tracks;
    const std::string bass = tracks.at(12).id.value;
    REQUIRE(tracks.at(12).name == "Bass");
    const std::string clip = rig.run("midi.addClip", {{"track", bass}, {"at", 0}}).at("id");
    rig.run("midi.addNote", {{"clip", clip}, {"pitch", 36}, {"start", 0}, {"channel", 11}});
    rig.run("track.armAll", {{"value", false}});

    rig.run("transport.play");
    rig.midi->inject(kMidiPort, Bytes{0x90, 64, 90}, rig.audio->hostTimeAt(rig.audio->processedFrames()));
    rig.audio->process(48000);
    REQUIRE(processUntil([&] { return rig.controller->engine().midiFlushed(); }));
    rig.run("transport.stop");
    const std::vector<Bytes> sent = sentTo(rig, kMidiPort);
    CHECK(std::ranges::contains(sent, Bytes{0x9A, 36, 100}));
    CHECK_FALSE(std::ranges::contains(sent, Bytes{0x90, 64, 90}));
}

TEST_CASE("a MIDI track on a keyboard channel takes the part name and icon") {
    YamahaRig rig({kKeyboard});
    const std::string id = rig.run("track.addMidi", {{"midiIn", {{"port", kKeyboard}, {"channel", 11}}}}).at("id");
    CHECK(rig.track(id).name == "Bass");
    CHECK(rig.track(id).icon == "guitar");
    rig.run("track.setMidiIn", {{"track", id}, {"channel", 14}});
    CHECK(rig.track(id).name == "Pad");
    CHECK(rig.run("track.addMidi", {{"name", "Lead"}, {"midiIn", {{"port", kKeyboard}, {"channel", 1}}}}).at("name") ==
          "Lead");
}

TEST_CASE("a chart imports from a MIDI take into a Chords and Style pair") {
    YamahaRig rig;
    rig.run("yamaha.chart.import", {{"path", dataPath("take.mid")}, {"shiftBars", 1}});
    const auto& tracks = rig.controller->project().document().tracks;
    REQUIRE(tracks.size() == 2);
    const Chart chart{tracks.at(0).data.get<std::vector<ChordEvent>>(),
                      tracks.at(1).data.get<std::vector<SectionEvent>>()};
    CHECK(chartText(chart, rig.controller->project().document().tempo) == readText("take-shift1.txt"));
    CHECK(tracks.at(0).midiIn.sysex);
    rig.run("project.undo");
    CHECK(rig.controller->project().document().tracks.empty());
    CHECK_THROWS_AS(rig.run("yamaha.chart.import", {{"path", dataPath("voices.txt")}}), app::AppError);
}

TEST_CASE("the voice table loads into the project, saves, reopens and undoes") {
    YamahaRig rig;
    const Json loaded = rig.run("yamaha.voices.load", {{"path", dataPath("voices.txt")}});
    REQUIRE(loaded.at("voices").size() == 3);
    CHECK(loaded.at("voices").at(1).at("name") == "Finger Bass");

    rig.run("project.saveAs", {{"path", rig.path("song").string()}});
    rig.run("project.new");
    CHECK(rig.controller->project().document().pluginData.empty());
    rig.run("project.open", {{"path", rig.path("song.erk").string()}});
    CHECK(rig.controller->project().document().pluginData.at("yamaha").at("voices") == loaded.at("voices"));

    rig.run("yamaha.voices.load", {{"path", dataPath("voices.txt")}});
    CHECK(rig.controller->project().document().pluginData.at("yamaha").at("voices").size() == 3);
    CHECK_FALSE(rig.controller->project().undoName());
    CHECK_THROWS_AS(rig.run("yamaha.voices.load", {{"path", dataPath("take-shift1.txt")}}), app::AppError);
}

TEST_CASE("the voice table goes to the keyboard on request and before play") {
    YamahaRig rig;
    CHECK_THROWS_AS(rig.run("yamaha.voices.send"), app::AppError);
    rig.run("yamaha.setupArranger");
    CHECK_THROWS_AS(rig.run("yamaha.voices.send"), app::AppError);
    rig.run("yamaha.voices.load", {{"path", dataPath("voices.txt")}});

    CHECK(rig.run("yamaha.voices.send").at("messages") == 10);
    const std::vector<Bytes> sent = sentTo(rig, kMidiPort);
    REQUIRE(sent.size() == 10);
    CHECK(sent.at(0) == Bytes{0xB8, 0, 127});
    CHECK(sent.at(2) == Bytes{0xC8, 0});
    CHECK(sent.at(7) == Bytes{0xCA, 33});
    CHECK_FALSE(std::ranges::any_of(sent, [](const Bytes& bytes) { return bytes.at(0) == 0xBF; }));

    const std::string style = rig.controller->project().document().tracks.at(1).id.value;
    rig.run("track.setMidiOut", {{"track", style}, {"port", kMidiPort}, {"channel", 1}});
    rig.midi->clearSent();
    CHECK(rig.run("yamaha.voices.send").at("messages") == 15);
    CHECK(sentTo(rig, kMidiPort).back() == Bytes{0xBF, 10, 58});

    rig.midi->clearSent();
    rig.run("transport.play");
    rig.run("transport.stop");
    CHECK(sentVoices(rig));

    rig.midi->clearSent();
    rig.registry.setSetting("yamaha.sendVoicesOnPlay", false);
    rig.run("transport.play");
    rig.run("transport.stop");
    CHECK_FALSE(sentVoices(rig));
}

TEST_CASE("chord sysex recorded from the keyboard lands on the Chords track and plays back as Chord Control") {
    YamahaRig rig;
    rig.run("yamaha.setupArranger");
    const std::string chords = rig.controller->project().document().tracks.at(0).id.value;
    rig.run("track.armAll", {{"value", false}});
    rig.run("track.setArm", {{"track", chords}, {"value", true}});

    rig.run("transport.record");
    rig.audio->process(kBlock);
    const auto now = [&] { return rig.audio->hostTimeAt(rig.audio->processedFrames()); };
    rig.midi->inject(kMidiPort, Bytes{0xF0, 0x43, 0x7E, 0x02, 0x32, 0x08, 0x7F, 0x7F, 0xF7}, now());
    rig.audio->process(48000);
    rig.midi->inject(kMidiPort, Bytes{0xF0, 0x43, 0x7E, 0x02, 0x35, 0x13, 0x7F, 0x7F, 0xF7}, now());
    rig.audio->process(48000);
    rig.run("transport.stop");
    rig.audio->process(kBlock);
    REQUIRE(rig.waitForTakes(1) == 1);
    const Json data = rig.track(chords).data;
    REQUIRE(data.size() == 2);
    CHECK(data.at(0).at("chord") == "Dm");
    CHECK(data.at(1).at("chord") == "G7");

    rig.run("track.setArm", {{"track", chords}, {"value", false}});
    rig.run("yamaha.chord.set", {{"track", chords}, {"at", data.at(1).at("at")}, {"chord", "Gsus4"}});
    rig.midi->clearSent();
    rig.run("transport.locate", {{"at", 0}});
    rig.run("transport.play");
    rig.audio->process(96000);
    REQUIRE(processUntil([&] { return rig.controller->engine().midiFlushed(); }));
    rig.run("transport.stop");
    const std::vector<Bytes> sent = sentTo(rig, kMidiPort);
    CHECK(std::ranges::contains(sent, Bytes{0xF0, 0x43, 0x7E, 0x02, 0x32, 0x08, 0x7F, 0x7F, 0xF7}));
    CHECK(std::ranges::contains(sent, Bytes{0xF0, 0x43, 0x7E, 0x02, 0x35, 0x20, 0x7F, 0x7F, 0xF7}));
    CHECK_FALSE(std::ranges::any_of(sent, [](const Bytes& bytes) { return (bytes.at(0) & 0xF0) == 0x90; }));
}

TEST_CASE("setting up the arranger publishes the session once without listing devices") {
    YamahaRig rig;
    const app::test::InspectorBindings inspector(*rig.controller);
    REQUIRE(inspector.object);
    int published = 0;
    QObject::connect(rig.controller.get(), &app::ProjectController::projectChanged, [&] { ++published; });
    const int audioListings = rig.audioLister->listings;
    const int midiListings = rig.midiLister->listings;

    const Json state = rig.runAsAgent("yamaha.setupArranger").at("state");

    CHECK(state.at("project").at("tracks").size() == 18);
    CHECK(rig.audioLister->listings == audioListings);
    CHECK(rig.midiLister->listings == midiListings);
    CHECK(published == 1);
}

TEST_CASE("the Style track presses the keyboard's section buttons on the External Controller port") {
    YamahaRig rig({kKeyboard, kController});
    rig.run("yamaha.setupArranger");
    const auto& tracks = rig.controller->project().document().tracks;
    const std::string chords = tracks.at(0).id.value;
    const std::string style = tracks.at(1).id.value;
    rig.run("track.armAll", {{"value", false}});
    rig.run("yamaha.chord.set", {{"track", chords}, {"at", 0}, {"chord", "Dm"}});
    rig.run("yamaha.section.set", {{"track", style}, {"at", 0}, {"name", "Main B"}});

    rig.run("transport.play");
    rig.audio->process(48000);
    REQUIRE(processUntil([&] { return rig.controller->engine().midiFlushed(); }));
    rig.run("transport.stop");
    CHECK(std::ranges::contains(sentTo(rig, kKeyboard), Bytes{0xBF, 12, 127}));
    CHECK(std::ranges::contains(sentTo(rig, kKeyboard), Bytes{0xBF, 12, 0}));
    CHECK(std::ranges::contains(sentTo(rig, kKeyboard), Bytes{0xF0, 0x43, 0x7E, 0x02, 0x32, 0x08, 0x7F, 0x7F, 0xF7}));
    CHECK_FALSE(std::ranges::contains(sentTo(rig, kController), Bytes{0xBF, 12, 127}));
}

TEST_CASE("the keyboard check names the setting for each part the playing Style does not send") {
    YamahaRig rig({kKeyboard, kController});
    CHECK(rig.run("yamaha.setupArranger").at("hints").empty());
    const auto hints = [&] { return rig.run("yamaha.keyboard.check").at("hints"); };
    const std::string style = rig.controller->project().document().tracks.at(1).id.value;
    rig.run("track.setMidiOut", {{"track", style}, {"port", kController}, {"channel", 16}});
    REQUIRE(hints().size() == 1);
    CHECK(hints().at(0).get<std::string>().ends_with("so part playback presses no Style buttons"));
    rig.run("track.setMidiOut", {{"track", style}, {"port", kKeyboard}, {"channel", 16}});
    CHECK(hints().empty());

    const auto play = [&](int channel, int notes) {
        for (int note = 0; note < notes; ++note) {
            rig.midi->inject(kKeyboard, Bytes{core::midi::statusFor(core::midi::kNoteOn, channel), 60, 100}, 0);
        }
    };
    play(4, 4);
    play(11, 48);
    REQUIRE(processUntil([&] { return hints().size() == 2; }));
    CHECK(hints().at(0).get<std::string>().ends_with("so the keyboard sends its Rhythm parts"));
    CHECK(hints().at(1).get<std::string>().ends_with("so the keyboard sends its chords"));

    rig.run("transport.play");
    rig.run("transport.stop");
    play(9, 1);
    play(4, 1);
    rig.midi->inject(kKeyboard, Bytes{0xF0, 0x43, 0x7E, 0x02, 0x32, 0x08, 0x7F, 0x7F, 0xF7}, 0);
    play(12, 48);
    REQUIRE(processUntil([&] { return hints().empty(); }));
}

TEST_CASE("play from inside a pattern starts at its pass with the section and chord chased and shows the bar") {
    YamahaRig rig({kKeyboard, kController});
    rig.run("yamaha.setupArranger");
    rig.run("track.armAll", {{"value", false}});
    const auto& tracks = rig.controller->project().document().tracks;
    const std::string chords = tracks.at(0).id.value;
    const std::string style = tracks.at(1).id.value;
    const core::Ticks bar = rig.controller->ticksPerBar();
    rig.run("yamaha.section.set", {{"track", style}, {"at", 0}, {"name", "Main A"}});
    rig.run("yamaha.section.setBars", {{"track", style}, {"at", 0}, {"bars", 4}});
    rig.run("yamaha.section.set", {{"track", style}, {"at", 8 * bar}, {"name", "Main B"}});
    rig.run("yamaha.chord.set", {{"track", chords}, {"at", 0}, {"chord", "Dm"}});
    rig.run("yamaha.chord.set", {{"track", chords}, {"at", 6 * bar}, {"chord", "G7"}});

    rig.run("transport.locate", {{"at", (6 * bar) + 100}});
    rig.midi->clearSent();
    rig.run("transport.play");
    CHECK(rig.controller->startReason() == "Style starts at bar 5, Main A");
    const std::vector<Bytes> sent = sentTo(rig, kKeyboard);
    CHECK(std::ranges::contains(sent, Bytes{0xBF, 11, 127}));
    CHECK(std::ranges::contains(sent, Bytes{0xF0, 0x43, 0x7E, 0x02, 0x32, 0x08, 0x7F, 0x7F, 0xF7}));
    rig.audio->process(4800);
    REQUIRE(processUntil([&] { return rig.controller->syncStatus().contains("yamaha"); }));
    CHECK(rig.controller->syncStatus().at("yamaha").label == "Main A 1/4 Dm");
    rig.run("transport.stop");
    REQUIRE(processUntil([&] { return !rig.controller->syncStatus().contains("yamaha"); }));

    rig.run("track.setMute", {{"track", style}, {"value", true}});
    rig.run("transport.locate", {{"at", (6 * bar) + 100}});
    rig.run("transport.play");
    CHECK(rig.controller->startReason().isEmpty());
    rig.run("transport.stop");
}

TEST_CASE("the panic stops the keyboard's Style only while it runs") {
    YamahaRig rig({kKeyboard, kController});
    rig.run("yamaha.setupArranger");
    const auto stopPressed = [&] {
        rig.midi->clearSent();
        rig.run("transport.panic");
        return processUntil([&] { return std::ranges::contains(sentTo(rig, kKeyboard), Bytes{0xBF, 0, 127}); }, 300);
    };
    const auto settle = [] { processUntil([] { return false; }, 100); };
    settle();
    CHECK_FALSE(stopPressed());
    rig.midi->inject(kKeyboard, Bytes{0x9A, 36, 100}, 0);
    settle();
    CHECK(stopPressed());
}

TEST_CASE("a muted Chords track sends no chords and a muted Style track presses only Stop on a running Style") {
    YamahaRig rig({kKeyboard, kController});
    rig.run("yamaha.setupArranger");
    rig.run("track.armAll", {{"value", false}});
    const auto& tracks = rig.controller->project().document().tracks;
    const std::string chords = tracks.at(0).id.value;
    const std::string style = tracks.at(1).id.value;
    rig.run("yamaha.chord.set", {{"track", chords}, {"at", 0}, {"chord", "Dm"}});
    rig.run("yamaha.section.set", {{"track", style}, {"at", 0}, {"name", "Main B"}});
    const auto settle = [] { processUntil([] { return false; }, 100); };
    const auto sentAny = [&](const Bytes& message) {
        return std::ranges::contains(sentTo(rig, kKeyboard), message) ||
               std::ranges::contains(sentTo(rig, kController), message);
    };
    const auto stopPressed = [&] { return sentAny(Bytes{0xBF, 0, 127}); };
    const Bytes chordSysex{0xF0, 0x43, 0x7E, 0x02, 0x32, 0x08, 0x7F, 0x7F, 0xF7};

    rig.run("track.setMute", {{"track", chords}, {"value", true}});
    rig.run("track.setMute", {{"track", style}, {"value", true}});
    rig.midi->clearSent();
    rig.run("transport.play");
    rig.audio->process(48000);
    settle();
    CHECK_FALSE(sentAny(Bytes{0xFA}));
    CHECK_FALSE(sentAny(chordSysex));
    CHECK_FALSE(sentAny(Bytes{0xBF, 11, 127}));
    CHECK_FALSE(stopPressed());
    rig.midi->inject(kKeyboard, Bytes{0x9A, 36, 100}, 0);
    CHECK(processUntil([&] { return stopPressed(); }));
    rig.run("transport.stop");

    rig.run("track.setMute", {{"track", style}, {"value", false}});
    rig.midi->clearSent();
    rig.run("transport.play");
    rig.midi->inject(kKeyboard, Bytes{0x9A, 36, 100}, 0);
    settle();
    rig.audio->process(4800);
    settle();
    CHECK(sentAny(Bytes{0xFA}));
    CHECK_FALSE(stopPressed());
    rig.run("track.setMute", {{"track", style}, {"value", true}});
    CHECK(processUntil([&] { return stopPressed(); }));
    rig.run("transport.stop");
}
