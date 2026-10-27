#include <sndfile.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <libremidi/writer.hpp>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

constexpr core::Ticks kBar = 3840;

const model::MidiClip& midiClip(const ProjectRig& rig, const Json& clip) {
    return std::get<model::MidiClip>(
        *rig.controller->project().findClip(model::ClipId{clip.at("id").get<std::string>()})->clip);
}

const model::Track& trackOf(const ProjectRig& rig, const Json& clip) {
    return *rig.controller->project().findClip(model::ClipId{clip.at("id").get<std::string>()})->track;
}

void addEvent(libremidi::writer& writer, int track, int delta, std::vector<std::uint8_t> bytes) {
    libremidi::message message;
    message.bytes = std::move(bytes);
    writer.add_event(delta, track, message);
}

// A type 0 file at 480 PPQ: a named track with notes on channel 1 and 3, and a program, a CC and a pitch bend on 3.
void writeTwoChannelFile(const std::filesystem::path& path) {
    libremidi::writer writer;
    writer.ticksPerQuarterNote = 480;
    addEvent(writer, 0, 0, {0xFF, 0x03, 4, 'L', 'e', 'a', 'd'});
    addEvent(writer, 0, 0, {0xC2, 33});
    addEvent(writer, 0, 0, {0x90, 60, 100});
    addEvent(writer, 0, 240, {0xB2, 7, 90});
    addEvent(writer, 0, 0, {0x92, 40, 80});
    addEvent(writer, 0, 240, {0x80, 60, 0});
    addEvent(writer, 0, 480, {0xE2, 0, 80});
    addEvent(writer, 0, 0, {0x92, 40, 0});
    std::ofstream out(path, std::ios::binary);
    writer.write(out);
}

}  // namespace

TEST_CASE("midi.import round trips a MIDI file written by export.midi") {
    ProjectRig rig;
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    const std::string clip = rig.run("midi.addClip", {{"track", keys}, {"at", 0}}).at("id");
    rig.run("midi.addNote", {{"clip", clip}, {"pitch", 60}, {"start", 0}, {"length", 480}, {"velocity", 90}});
    rig.run("midi.addNote", {{"clip", clip}, {"pitch", 64}, {"start", 960}, {"length", 240}, {"velocity", 70}});
    const std::string file = rig.path("song.mid").string();
    rig.run("export.midi", {{"path", file}});

    const Json imported = rig.run("midi.import", {{"path", file}, {"at", "3.1.0"}});

    REQUIRE(imported.at("clips").size() == 1);
    const Json& result = imported.at("clips").at(0);
    CHECK(trackOf(rig, result).name == "Keys");
    CHECK(trackOf(rig, result).kind == "midi");
    CHECK(trackOf(rig, result).id.value != keys);
    const model::MidiClip& midi = midiClip(rig, result);
    CHECK(midi.start == 2 * kBar);
    CHECK(midi.length == kBar);
    REQUIRE(midi.notes.size() == 2);
    CHECK(midi.notes[0].pitch == 60);
    CHECK(midi.notes[0].start == 0);
    CHECK(midi.notes[0].length == 480);
    CHECK(midi.notes[0].velocity == 90);
    CHECK(midi.notes[1].pitch == 64);
    CHECK(midi.notes[1].start == 960);
    CHECK(midi.notes[1].length == 240);
}

TEST_CASE("midi.import splits channels into tracks, scales ticks and keeps controller events, as one undo step") {
    ProjectRig rig;
    const std::filesystem::path file = rig.path("two.mid");
    writeTwoChannelFile(file);

    const Json imported = rig.run("midi.import", {{"path", file.string()}});

    CHECK(rig.controller->project().undoName() == "import two.mid");
    REQUIRE(imported.at("clips").size() == 2);
    CHECK(trackOf(rig, imported.at("clips").at(0)).name == "Lead ch 1");
    CHECK(trackOf(rig, imported.at("clips").at(1)).name == "Lead ch 3");
    const model::MidiClip& lead = midiClip(rig, imported.at("clips").at(0));
    REQUIRE(lead.notes.size() == 1);
    CHECK(lead.notes[0].length == 960);
    const model::MidiClip& bass = midiClip(rig, imported.at("clips").at(1));
    REQUIRE(bass.notes.size() == 1);
    CHECK(bass.notes[0].channel == 3);
    CHECK(bass.notes[0].start == 480);
    CHECK(bass.notes[0].length == 1440);
    CHECK(bass.events == std::vector<model::MidiEvent>{{.at = 0, .bytes = {0xC2, 33}},
                                                       {.at = 480, .bytes = {0xB2, 7, 90}},
                                                       {.at = 1920, .bytes = {0xE2, 0, 80}}});

    rig.run("project.undo");
    CHECK(rig.controller->project().document().tracks.empty());
}

TEST_CASE("midi.import puts a single part on the given MIDI track and refuses other tracks") {
    ProjectRig rig;
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    const std::string clip = rig.run("midi.addClip", {{"track", keys}, {"at", 0}}).at("id");
    rig.run("midi.addNote", {{"clip", clip}, {"pitch", 60}, {"start", 0}, {"length", 480}});
    const std::string file = rig.path("one.mid").string();
    rig.run("export.midi", {{"path", file}});

    const Json imported = rig.run("midi.import", {{"path", file}, {"track", keys}, {"at", kBar}});

    CHECK(trackOf(rig, imported.at("clips").at(0)).id.value == keys);
    CHECK(rig.controller->project().document().tracks.size() == 2);
    CHECK_THROWS_WITH(rig.run("midi.import", {{"path", file}, {"track", vox}}),
                      Catch::Matchers::ContainsSubstring("is not a MIDI track"));
    std::ofstream(rig.path("junk.mid")) << "not midi";
    CHECK_THROWS_AS(rig.run("midi.import", {{"path", rig.path("junk.mid").string()}}), AppError);
}

TEST_CASE("clip.import opens Ogg Vorbis, Opus and MP3, makes a new track on request and names files it cannot open") {
    ProjectRig rig;
    const std::vector<std::pair<std::string, int>> formats{{"tone.ogg", SF_FORMAT_OGG | SF_FORMAT_VORBIS},
                                                           {"tone.oga", SF_FORMAT_OGG | SF_FORMAT_OPUS},
                                                           {"tone.mp3", SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III}};
    for (const auto& [name, format] : formats) {
        SF_INFO info{.frames = 0, .samplerate = 48000, .channels = 1, .format = format, .sections = 0, .seekable = 0};
        SNDFILE* file = sf_open(rig.path(name).c_str(), SFM_WRITE, &info);
        REQUIRE(file != nullptr);
        const std::vector<float> samples(48000, 0.25F);
        sf_writef_float(file, samples.data(), static_cast<sf_count_t>(samples.size()));
        sf_close(file);

        const Json clip = rig.run("clip.import", {{"path", rig.path(name).string()}, {"newTrack", true}});

        CHECK(trackOf(rig, clip).name == std::filesystem::path{name}.stem().string());
        CHECK(trackOf(rig, clip).kind == "audio");
        CHECK(rig.controller->project().undoName() == "import " + name);
    }
    CHECK(rig.controller->project().document().tracks.size() == 3);

    std::ofstream(rig.path("junk.wav")) << "not audio";
    CHECK_THROWS_WITH(rig.run("clip.import", {{"path", rig.path("junk.wav").string()}, {"newTrack", true}}),
                      Catch::Matchers::ContainsSubstring("cannot open junk.wav as audio"));
    CHECK(rig.controller->project().document().tracks.size() == 3);
}
