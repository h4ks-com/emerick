#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

#include "emerick/app/backends.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

void recordWhile(ProjectRig& rig, const std::function<void()>& during) {
    rig.run("transport.record");
    rig.audio->process(kBlock);
    during();
    rig.run("transport.stop");
    rig.audio->process(kBlock);
}

}  // namespace

TEST_CASE("a virtual MIDI take becomes a MIDI clip, takes stack and undo removes them") {
    ProjectRig rig;
    const std::string id = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    CHECK(rig.run("track.setArm", {{"track", id}, {"value", true}}).at("midiIn").at("port") == kMidiPort);

    const auto playNote = [&] {
        const auto now = [&] { return rig.audio->hostTimeAt(rig.audio->processedFrames()); };
        rig.midi->inject(kMidiPort, std::vector<std::uint8_t>{0x90, 60, 100}, now());
        rig.audio->process(24000);
        rig.midi->inject(kMidiPort, std::vector<std::uint8_t>{0x80, 60, 0}, now());
        rig.audio->process(24000);
    };
    recordWhile(rig, playNote);
    REQUIRE(rig.waitForTakes(1) == 1);
    Json clip = projectState(*rig.controller).at("tracks").at(0).at("clips").at(0);
    CHECK(clip.at("type") == "midi");
    CHECK(clip.at("eventCount") == 2);
    CHECK(clip.at("start") == 0);
    CHECK(clip.at("length") >= 1920);
    CHECK_FALSE(clip.contains("events"));
    const Json events = rig.run("project.clipEvents", {{"clip", clip.at("id")}}).at("events");
    CHECK(events.at(0).at("bytes") == Json::array({0x90, 60, 100}));
    CHECK(events.at(1).at("at").get<int>() >= 900);

    rig.run("transport.locate", {{"position", "1.1.0"}});
    recordWhile(rig, playNote);
    REQUIRE(rig.waitForTakes(1) == 1);
    Json state = projectState(*rig.controller);
    CHECK(state.at("tracks").at(0).at("clips").size() == 2);
    CHECK(state.at("undo") == "record take on Keys");

    state = rig.run("project.undo");
    CHECK(state.at("tracks").at(0).at("clips").size() == 1);
    CHECK(state.at("tracks").at(0).at("clips").at(0).at("id") == clip.at("id"));
}

TEST_CASE("an offline audio take becomes an audio clip file under the project folder") {
    ProjectRig rig;
    const std::string id = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    rig.run("track.setArm", {{"track", id}, {"value", true}});
    recordWhile(rig, [&] {
        rig.audio->setInput("interface", 1, std::vector<float>(48000, 0.25F));
        rig.audio->process(48000);
    });
    REQUIRE(rig.waitForTakes(1) == 1);

    const Json clip = projectState(*rig.controller).at("tracks").at(0).at("clips").at(0);
    CHECK(clip.at("type") == "audio");
    const std::filesystem::path file = rig.controller->folder() / clip.at("file").get<std::string>();
    CHECK(clip.at("file").get<std::string>().starts_with("audio/"));
    const auto info = readAudioInfo(file);
    REQUIRE(info);
    CHECK(info->frames >= 48000);
    CHECK(clip.at("length") == rig.controller->project().document().tempo.framesToTicks(info->frames, kRate));

    recordWhile(rig, [&] { rig.audio->process(4800); });
    REQUIRE(rig.waitForTakes(1) == 1);
    const Json clips = projectState(*rig.controller).at("tracks").at(0).at("clips");
    REQUIRE(clips.size() == 2);
    CHECK(clips.at(0).at("file") != clips.at(1).at("file"));
}

TEST_CASE("a clipped input lights the track's meter and the take reports it") {
    ProjectRig rig;
    const std::string id = rig.run("track.addAudio", {{"name", "Keys"}}).at("id");
    rig.run("track.setArm", {{"track", id}, {"value", true}});
    recordWhile(rig, [&] {
        rig.audio->setInput("interface", 1, {0.5F, 1.0F, -1.0F, 0.25F});
        rig.audio->process(4800);
        CHECK(processUntil([&] {
            const Json meters = transportState(*rig.controller).at("meters");
            return meters.contains(id) && meters.at(id).at("input").at("clipped") == true;
        }));
    });
    REQUIRE(rig.waitForTakes(1) == 1);

    const Json errors = projectState(*rig.controller).at("takeErrors");
    REQUIRE(errors.size() == 1);
    CHECK(errors.at(0).at("message").get<std::string>().starts_with("the input reached full scale on 2 samples"));
}

TEST_CASE("a device that stops calling back stops the transport, keeps the take and reports it until it runs again") {
    ProjectRig rig;
    const std::string id = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    rig.run("track.setArm", {{"track", id}, {"value", true}});
    rig.run("transport.record");
    rig.audio->setInput("interface", 1, std::vector<float>(4800, 0.25F));
    rig.audio->process(4800);
    rig.clock.stop();

    REQUIRE(processUntil([&] { return !rig.controller->streamError().isEmpty(); }, 4000));
    const Json transport = transportState(*rig.controller);
    CHECK(transport.at("streamError").get<std::string>().starts_with("The audio device stopped responding"));
    CHECK(transport.at("playing") == false);
    CHECK(transport.at("recording") == false);
    REQUIRE(rig.waitForTakes(1) == 1);
    CHECK(projectState(*rig.controller).at("tracks").at(0).at("clips").size() == 1);

    rig.clock.start(1);
    rig.run("transport.play");
    CHECK(processUntil([&] { return rig.controller->streamError().isEmpty(); }));
}

TEST_CASE("midi.inject feeds a virtual port and midi.sent lists what the app sent") {
    ProjectRig rig;
    registerVirtualMidiActions(rig.registry, *rig.midi);
    rig.run("track.addMidi", {{"midiOut", {{"port", kMidiPort}, {"channel", 2}, {"thru", true}}}});
    CHECK_THROWS_AS(rig.run("midi.inject", {{"port", kMidiPort}, {"bytes", Json::array()}}), AppError);

    rig.controller->flush();
    rig.run("midi.inject", {{"port", kMidiPort}, {"bytes", {0x90, 60, 100}}});
    rig.audio->process(kBlock);
    REQUIRE(processUntil([&] { return rig.controller->engine().midiFlushed(); }));
    CHECK(rig.run("midi.sent", {{"port", kMidiPort}, {"clear", true}}).at("messages") ==
          Json::array({Json::array({0x91, 60, 100})}));
    CHECK(rig.run("midi.sent", {{"port", kMidiPort}}).at("messages").empty());
}
