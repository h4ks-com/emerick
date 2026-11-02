#include <sndfile.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <ranges>
#include <span>
#include <string>
#include <vector>

#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/engine/sound_file.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

struct SourceRig : ProjectRig {
    SourceRig() {
        registerPluginActions(registry, nav, catalog);
        registerInstrumentActions(registry, *controller, nav, catalog);
        registry.setSetting(kClapFoldersSetting, Json::array({EMERICK_TEST_CLAP_FOLDER}));
    }

    std::string addDevice() {
        const std::string id =
            run("track.addInstrument", {{"plugin", "org.emerick.test.sine"}, {"name", "Style"}}).at("id");
        controller->project().editTrack(model::TrackId{id}, "add notes", [](model::Track& track) {
            model::MidiClip clip;
            clip.length = 1920;
            clip.notes = {{.pitch = 60, .velocity = 100, .start = 480, .length = 240, .channel = 1},
                          {.pitch = 62, .velocity = 90, .start = 960, .length = 240, .channel = 2}};
            track.clips.emplace_back(clip);
        });
        controller->flush();
        return id;
    }

    // The test folder holds a bundle that hangs, so a short timeout keeps each scan quick.
    PluginCatalog catalog{{.cacheFolder = path("cache"),
                           .scanner = EMERICK_PLUGIN_SCANNER,
                           .folders = {},
                           .timeout = std::chrono::milliseconds(500)}};
};

std::vector<float> readLeft(const std::filesystem::path& path) {
    SF_INFO info{};
    SNDFILE* file = engine::openSoundFile(path, SFM_READ, &info);
    REQUIRE(file != nullptr);
    std::vector<float> interleaved(static_cast<std::size_t>(info.frames * info.channels));
    sf_readf_float(file, interleaved.data(), info.frames);
    sf_close(file);
    std::vector<float> left;
    for (std::size_t at = 0; at < interleaved.size(); at += static_cast<std::size_t>(info.channels)) {
        left.push_back(interleaved[at]);
    }
    return left;
}

float loudest(const std::vector<float>& samples, std::size_t from, std::size_t to) {
    return std::ranges::max(std::span(samples).subspan(from, to - from) |
                            std::views::transform([](float sample) { return std::abs(sample); }));
}

}  // namespace

TEST_CASE("export mix and stems play an instrument track's clips through its instrument") {
    SourceRig rig;
    rig.addDevice();
    const auto folder = rig.path("export");
    rig.run("export.mix", {{"path", (folder / "mix.wav").string()}});
    const std::vector<float> mix = readLeft(folder / "mix.wav");
    REQUIRE(mix.size() == 48000);
    CHECK(loudest(mix, 0, 11000) == 0.0F);
    CHECK(loudest(mix, 12000, 48000) > 0.1F);

    REQUIRE(rig.run("export.stems", {{"folder", folder.string()}}).at("files").size() == 1);
    CHECK(loudest(readLeft(folder / "Style.wav"), 12000, 48000) > 0.1F);
}

TEST_CASE("a stem of a track that takes a device's MIDI plays what the device sends it, and the device gets no stem") {
    SourceRig rig;
    const std::string device = rig.addDevice();
    const std::string keys = rig.run("track.addInstrument", {{"plugin", "org.emerick.test.sine"}, {"name", "Keys"}})
                                 .at("id")
                                 .get<std::string>();
    rig.run("track.setMidiIn", {{"track", keys}, {"sourceTrack", device}, {"channel", 2}});

    for (const auto& tracks : {Json::array(), Json::array({"Keys"})}) {
        const auto folder = rig.path(tracks.empty() ? "all" : "keys");
        Json params{{"folder", folder.string()}};
        if (!tracks.empty()) {
            params["tracks"] = tracks;
        }
        REQUIRE(rig.run("export.stems", params).at("files").size() == 1);
        const std::vector<float> stem = readLeft(folder / "Keys.wav");
        CHECK(loudest(stem, 0, 23000) == 0.0F);
        CHECK(loudest(stem, 24000, 48000) > 0.1F);
    }
}

TEST_CASE("soloing a track that takes a device's MIDI keeps the device playing it") {
    SourceRig rig;
    const std::string device = rig.addDevice();
    const std::string keys = rig.run("track.addInstrument", {{"plugin", "org.emerick.test.sine"}, {"name", "Keys"}})
                                 .at("id")
                                 .get<std::string>();
    rig.run("track.setMidiIn", {{"track", keys}, {"sourceTrack", device}, {"channel", 2}});
    rig.run("track.setSolo", {{"track", keys}, {"value", true}});

    const auto folder = rig.path("solo");
    rig.run("export.mix", {{"path", (folder / "mix.wav").string()}});
    CHECK(loudest(readLeft(folder / "mix.wav"), 24000, 48000) > 0.1F);
}

TEST_CASE("a MIDI input takes a device's output in place of its port, and undo gives the port back") {
    SourceRig rig;
    const std::string device = rig.addDevice();
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    const QVariantList sources = rig.controller->midiSources();
    REQUIRE(sources.size() == 1);
    CHECK(sources[0].toMap().value("name").toString() == "Style / Test Sine");
    CHECK(sources[0].toMap().value("id").toString().toStdString() == device);

    const Json midiIn = rig.run("track.setMidiIn", {{"track", keys}, {"sourceTrack", device}}).at("midiIn");
    CHECK(midiIn.at("sourceTrack") == device);
    CHECK(midiIn.at("port") == "");
    CHECK(rig.run("track.setMidiIn", {{"track", keys}, {"channel", 2}}).at("midiIn").at("sourceTrack") == device);

    rig.run("project.undo");
    rig.run("project.undo");
    CHECK(rig.controller->project().track(model::TrackId{keys})->midiIn.portId == kMidiPort);
    CHECK(rig.controller->project().track(model::TrackId{keys})->midiIn.sourceTrack.empty());
    rig.run("project.redo");
    CHECK(rig.controller->project().track(model::TrackId{keys})->midiIn.sourceTrack.value == device);

    rig.run("track.setMidiIn", {{"track", keys}, {"port", kMidiPort}});
    CHECK(rig.controller->project().track(model::TrackId{keys})->midiIn.sourceTrack.empty());
}

TEST_CASE("a MIDI input refuses a track without an instrument, itself and a loop") {
    SourceRig rig;
    const std::string device = rig.addDevice();
    const std::string other = rig.addDevice();
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    CHECK_THROWS_AS(rig.run("track.setMidiIn", {{"track", device}, {"sourceTrack", keys}}), AppError);
    CHECK_THROWS_AS(rig.run("track.setMidiIn", {{"track", device}, {"sourceTrack", device}}), AppError);
    rig.run("track.setMidiIn", {{"track", other}, {"sourceTrack", device}});
    CHECK_THROWS_AS(rig.run("track.setMidiIn", {{"track", device}, {"sourceTrack", other}}), AppError);
    CHECK_THROWS_AS(rig.run("track.setMidiOut", {{"track", keys}, {"sourceTrack", device}}), AppError);
}

TEST_CASE("a project saves a track's device input and loads it back") {
    SourceRig rig;
    const std::string device = rig.addDevice();
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    rig.run("track.setMidiIn", {{"track", keys}, {"sourceTrack", device}, {"channel", 2}});
    rig.controller->save();

    SourceRig other;
    other.controller->open(rig.controller->folder());
    const model::MidiRoute& midiIn = other.controller->project().track(model::TrackId{keys})->midiIn;
    CHECK(midiIn.sourceTrack.value == device);
    CHECK(midiIn.channel == 2);
}

TEST_CASE("recording a track on one channel of a device records that channel of what the device plays") {
    SourceRig rig;
    const std::string device = rig.addDevice();
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    rig.run("track.setMidiIn", {{"track", keys}, {"sourceTrack", device}, {"channel", 2}});
    rig.run("track.setArm", {{"track", keys}, {"value", true}});
    rig.run("transport.record");
    rig.audio->process(48000);
    rig.run("transport.stop");
    rig.audio->process(kBlock);
    REQUIRE(rig.waitForTakes(1) == 1);

    const Json clip = projectState(*rig.controller).at("tracks").at(1).at("clips").at(0);
    const Json notes = rig.run("project.clipEvents", {{"clip", clip.at("id")}}).at("notes");
    REQUIRE(notes.size() == 1);
    CHECK(notes.at(0).at("pitch") == 62);
    CHECK(notes.at(0).at("velocity") == 90);
    CHECK(notes.at(0).at("start") == 960);
}

TEST_CASE("removing a device clears the inputs that take its MIDI") {
    SourceRig rig;
    const std::string device = rig.addDevice();
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    rig.run("track.setMidiIn", {{"track", keys}, {"sourceTrack", device}});
    rig.run("track.remove", {{"track", device}});
    CHECK(rig.controller->project().track(model::TrackId{keys})->midiIn.sourceTrack.empty());
}
