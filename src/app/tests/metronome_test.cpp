#include <sndfile.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

bool anySound(const std::vector<float>& samples) {
    return std::ranges::any_of(samples, [](float sample) { return sample != 0.0F; });
}

std::vector<float> readSamples(const std::filesystem::path& path) {
    SF_INFO info{};
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
    REQUIRE(file != nullptr);
    std::vector<float> samples(static_cast<std::size_t>(info.frames * info.channels));
    sf_readf_float(file, samples.data(), info.frames);
    sf_close(file);
    return samples;
}

}  // namespace

TEST_CASE("metronome actions and settings round trip through the state provider") {
    ProjectRig rig;
    CHECK(rig.registry.action("metronome.toggle")->shortcut == "K");
    CHECK(rig.run("metronome.toggle").at("enabled") == true);
    CHECK(rig.run("metronome.setMode", {{"mode", "Drum groove"}}).at("mode") == "Drum groove");
    CHECK(rig.run("metronome.setPattern", {{"pattern", "Bossa"}}).at("pattern") == "Bossa");
    CHECK(rig.run("metronome.setCountIn", {{"bars", 2}}).at("countIn") == 2);
    CHECK(rig.run("metronome.setVolume", {{"db", -12.5}}).at("volume") == -12.5);
    CHECK_THROWS_AS(rig.run("metronome.setMode", {{"mode", "cowbell"}}), AppError);
    CHECK_THROWS_AS(rig.run("metronome.setCountIn", {{"bars", 5}}), AppError);
    rig.registry.setSetting("metronome.note.kick", 35);

    const Json state = rig.registry.state("metronome");
    CHECK(state.at("enabled") == true);
    CHECK(state.at("countIn") == 2);
    CHECK(state.at("note.kick") == 35);
    CHECK(state.at("midiChannel") == 10);
    CHECK(state.at("errors").empty());
    CHECK(rig.run("metronome.toggle").at("enabled") == false);
    CHECK(static_cast<std::size_t>(rig.registry.settingsAt("settings.metronome").size()) ==
          rig.registry.state("metronome").size() - 1);
}

TEST_CASE("the metronome menu offers the toggle, every sound, every pattern and the count-in") {
    ProjectRig rig;
    const QVariantList items = rig.registry.menus().value(kMetronomeMenu).toList();
    REQUIRE(items.size() == 4);
    CHECK(items[0].toMap().value("id") == "metronome.toggle");
    CHECK(items[1].toMap().value("choices").toList().size() == 5);
    CHECK(items[2].toMap().value("choices").toList().size() == 5);
    const QVariantList countIn = items[3].toMap().value("choices").toList();
    REQUIRE(countIn.size() == 5);
    CHECK(countIn[0].toMap().value("title") == "Off");
    CHECK(countIn[2].toMap().value("title") == "2 bars");
}

TEST_CASE("an enabled metronome clicks on its chosen output while the project plays") {
    ProjectRig rig;
    rig.registry.setSetting("metronome.duringPlayback", true);
    rig.registry.setSetting("metronome.outputDevice", kMotuId);
    rig.registry.setSetting("metronome.outputChannels", "3 4");
    rig.run("metronome.toggle");
    rig.run("transport.play");
    rig.audio->process(4800);
    rig.run("transport.stop");

    CHECK(anySound(rig.audio->output(kMotuId, 3)));
    CHECK(anySound(rig.audio->output(kMotuId, 4)));
    CHECK_FALSE(anySound(rig.audio->output(kMotuId, 1)));
    CHECK(rig.registry.state("metronome").at("errors").empty());

    rig.registry.setSetting("metronome.outputDevice", "gone");
    QCoreApplication::processEvents();
    CHECK(rig.registry.state("metronome").at("errors").size() == 1);
}

TEST_CASE("the MIDI click goes to the chosen port and channel") {
    ProjectRig rig;
    rig.registry.setSetting("metronome.mode", "MIDI click");
    rig.registry.setSetting("metronome.midiPort", kMidiPort);
    rig.registry.setSetting("metronome.midiChannel", 3);
    rig.registry.setSetting("metronome.duringPlayback", true);
    rig.run("metronome.toggle");
    rig.run("transport.play");
    rig.audio->process(48000);
    rig.run("transport.stop");
    REQUIRE(processUntil([&] { return rig.controller->engine().midiFlushed(); }));

    const auto sent = rig.midi->sent(kMidiPort);
    const auto first = std::ranges::find_if(
        sent, [](const engine::SentMidi& message) { return message.bytes.size() == 3 && message.bytes[0] == 0x92; });
    REQUIRE(first != sent.end());
    CHECK(first->bytes[1] == 76);
}

TEST_CASE("a count-in delays the take, which lands at the record position") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    rig.run("track.setArm", {{"track", vox}, {"value", true}});
    rig.run("metronome.setCountIn", {{"bars", 1}});
    rig.run("transport.locate", {{"at", "3.1.0"}});

    CHECK(rig.run("transport.record").at("playing") == true);
    rig.audio->process(48000);
    CHECK(rig.controller->positionTicks() == 2 * rig.controller->ticksPerBar());
    rig.audio->process(48000 + 4800);
    rig.run("transport.stop");
    rig.audio->process(kBlock);
    REQUIRE(rig.waitForTakes(1) == 1);

    const Json clip = projectState(*rig.controller).at("tracks").at(0).at("clips").at(0);
    CHECK(clip.at("start") == 2 * rig.controller->ticksPerBar());
}

TEST_CASE("exports never hold the metronome") {
    ProjectRig rig;
    rig.registry.setSetting("metronome.duringPlayback", true);
    rig.registry.setSetting("metronome.mode", "Drum groove");
    rig.run("metronome.toggle");
    const std::string pad = rig.run("track.addAudio", {{"name", "Pad"}}).at("id");
    writeConstant(rig.path("silence.wav"), 48000, 0.0F);
    rig.run("clip.import", {{"track", pad}, {"path", rig.path("silence.wav").string()}});

    const auto folder = rig.path("stems");
    rig.run("export.stems", {{"folder", folder.string()}});
    rig.run("export.mix", {{"path", (folder / "mix.wav").string()}});

    for (const char* name : {"Pad.wav", "mix.wav"}) {
        const std::vector<float> samples = readSamples(folder / name);
        CHECK(samples.size() >= 48000);
        CHECK_FALSE(anySound(samples));
    }
}
