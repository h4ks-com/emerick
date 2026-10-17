#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

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
    CHECK(tracks.at(1).at("midiOut").at("channel") == 4);
    CHECK(tracks.at(2).at("kind") == "yamaha.style");
    CHECK(tracks.at(2).at("midiOut") == Json{{"port", kMidiPort}, {"channel", 1}, {"thru", false}, {"sysex", false}});
    for (int channel = 1; channel <= 16; ++channel) {
        const Json& part = tracks.at(static_cast<std::size_t>(channel) + 2);
        CHECK(part.at("kind") == "midi");
        CHECK(part.at("midiIn") == Json{{"port", kMidiPort}, {"channel", channel}, {"thru", false}, {"sysex", false}});
        CHECK(part.at("midiOut").at("port") == "");
    }
    CHECK(tracks.at(3).at("name") == "Right 1");
    CHECK(tracks.at(3).at("icon") == "piano-keys");
    CHECK(tracks.at(13).at("name") == "Bass");
    CHECK(state.at("clockOut") == Json::array({kMidiPort}));

    rig.run("project.undo");
    CHECK(rig.controller->project().document().tracks.size() == 1);
    CHECK(rig.controller->project().document().clockPorts.empty());
    rig.run("project.redo");
    CHECK(rig.controller->project().document().tracks.size() == 19);
}

TEST_CASE("the arranger finds the keyboard port and the controller port by name") {
    YamahaRig rig({"Other", kKeyboard, kController});
    rig.run("yamaha.setupArranger");
    const auto& tracks = rig.controller->project().document().tracks;
    CHECK(tracks.at(0).midiIn.portId == kKeyboard);
    CHECK(tracks.at(1).midiOut.portId == kController);
    CHECK_THROWS_AS(rig.run("yamaha.setupArranger", {{"port", "Missing"}}), app::AppError);

    rig.registry.setSetting("yamaha.controllerPort", "Other");
    rig.run("yamaha.setupArranger", {{"port", kKeyboard}});
    CHECK(rig.controller->project().document().tracks.at(19).midiOut.portId == "Other");
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
    rig.run("project.open", {{"path", rig.path("song").string()}});
    CHECK(rig.controller->project().document().pluginData.at("yamaha").at("voices") == loaded.at("voices"));

    rig.run("yamaha.voices.load", {{"path", dataPath("voices.txt")}});
    rig.run("project.undo");
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

    CHECK(rig.run("yamaha.voices.send").at("messages") == 15);
    const std::vector<Bytes> sent = sentTo(rig, kMidiPort);
    REQUIRE(sent.size() == 15);
    CHECK(sent.at(0) == Bytes{0xB8, 0, 127});
    CHECK(sent.at(2) == Bytes{0xC8, 0});
    CHECK(sent.at(7) == Bytes{0xCA, 33});
    CHECK(sent.back() == Bytes{0xBF, 10, 58});

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

TEST_CASE("chord sysex recorded from the keyboard lands on the Chords track and plays back as block chords") {
    YamahaRig rig;
    rig.run("yamaha.setupArranger");
    const std::string chords = rig.controller->project().document().tracks.at(0).id.value;
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
    rig.run("transport.locate", {{"position", 0}});
    rig.run("transport.play");
    rig.audio->process(96000);
    REQUIRE(processUntil([&] { return rig.controller->engine().midiFlushed(); }));
    rig.run("transport.stop");
    const std::vector<Bytes> sent = sentTo(rig, kMidiPort);
    CHECK(std::ranges::contains(sent, Bytes{0x93, 50, 90}));
    CHECK(std::ranges::contains(sent, Bytes{0x93, 60, 90}));
}
