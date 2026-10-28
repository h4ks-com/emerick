#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "emerick/app/session_mapping.hpp"
#include "emerick/model/midi_notes.hpp"
#include "midi_file.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

constexpr core::Ticks kBar = 3840;

struct LoopRig {
    ProjectRig rig;
    std::string track = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    std::string clip = rig.run("midi.addClip", {{"track", track}, {"at", 1920}}).at("id");

    void add(int pitch, core::Ticks start) const {
        rig.run("midi.addNote", {{"clip", clip}, {"pitch", pitch}, {"start", start}, {"length", 480}});
    }

    [[nodiscard]] const model::MidiClip& midi(const std::string& id) const {
        return std::get<model::MidiClip>(*rig.controller->project().findClip(model::ClipId{id})->clip);
    }

    // Each note-on the session plays on the track, as its tick from the clip start and its pitch.
    [[nodiscard]] std::vector<std::pair<core::Ticks, int>> noteOns(std::size_t clipIndex = 0) const {
        const engine::Session session =
            toSession(rig.controller->project().document(), rig.registry, rig.controller->folder());
        std::vector<std::pair<core::Ticks, int>> result;
        for (const engine::MidiEvent& event : session.tracks.at(0).midiClips.at(clipIndex).events) {
            if (event.bytes.at(0) == 0x90) {
                result.emplace_back(event.tick, event.bytes.at(1));
            }
        }
        return result;
    }
};

using NoteOns = std::vector<std::pair<core::Ticks, int>>;

}  // namespace

TEST_CASE("Loop clip repeats a MIDI clip until the next clip in one undo step, and again ends the loop") {
    LoopRig keys;
    keys.add(60, 0);
    keys.rig.run("midi.addClip", {{"track", keys.track}, {"at", 1920 + (3 * kBar)}});
    keys.rig.run("selection.set", {{"clips", {keys.clip}}});

    const Json looped = keys.rig.run("clip.loop");
    CHECK(looped.at("clips").at(0).at("loopLength") == 3 * kBar);
    CHECK(keys.noteOns() == NoteOns{{0, 60}, {kBar, 60}, {2 * kBar, 60}});
    CHECK(keys.rig.controller->project().undoName() == "loop clip");

    keys.rig.run("clip.loop");
    CHECK_FALSE(model::looped(keys.midi(keys.clip)));
    keys.rig.run("project.undo");
    CHECK(keys.midi(keys.clip).loopLength == 3 * kBar);
    keys.rig.run("project.undo");
    CHECK(keys.midi(keys.clip).loopLength == 0);
}

TEST_CASE("Loop clip refuses a clip that already reaches the project end, and an audio clip") {
    LoopRig keys;
    CHECK_THROWS_WITH(keys.rig.run("clip.loop", {{"clip", keys.clip}}),
                      Catch::Matchers::ContainsSubstring("drag its loop handle"));
    const std::string vox = keys.rig.run("track.addAudio").at("id");
    writeConstant(keys.rig.path("ones.wav"), 48000, 1.0F);
    const std::string audio =
        keys.rig.run("clip.import", {{"track", vox}, {"path", keys.rig.path("ones.wav").string()}}).at("id");
    CHECK_THROWS_WITH(keys.rig.run("clip.setLoopLength", {{"clip", audio}, {"length", kBar}}),
                      Catch::Matchers::ContainsSubstring("only MIDI clips loop"));
}

TEST_CASE("a loop length plays a partial last repeat, follows content edits and extends the project and its export") {
    LoopRig keys;
    keys.add(60, 0);
    keys.add(64, 1920);
    keys.rig.run("clip.setLoopLength", {{"clip", keys.clip}, {"length", (2 * kBar) + 960}});
    CHECK(keys.noteOns() == NoteOns{{0, 60}, {1920, 64}, {kBar, 60}, {kBar + 1920, 64}, {2 * kBar, 60}});
    CHECK(keys.rig.controller->endTicks() == 1920 + (2 * kBar) + 960);

    keys.add(67, 480);
    CHECK(keys.noteOns() == NoteOns{{0, 60},
                                    {480, 67},
                                    {1920, 64},
                                    {kBar, 60},
                                    {kBar + 480, 67},
                                    {kBar + 1920, 64},
                                    {2 * kBar, 60},
                                    {(2 * kBar) + 480, 67}});

    keys.rig.run("export.logic", {{"folder", keys.rig.path("out").string()}});
    const libremidi::reader midi = readMidiFile(keys.rig.path("out") / "Untitled 1 for Logic" / "MIDI.mid");
    std::vector<int> ons;
    for (const auto& event : midi.tracks.at(1)) {
        if (!event.m.is_meta_event() && event.m.bytes.at(0) == 0x90) {
            ons.push_back(event.tick);
        }
    }
    CHECK(ons == std::vector<int>{1920, 2400, 3840, 5760, 6240, 7680, 9600, 10080});
}

TEST_CASE(
    "splitting a looped clip at the playhead keeps the loop on the left and writes out the repeats on the right") {
    LoopRig keys;
    keys.add(60, 0);
    keys.add(64, 1920);
    keys.rig.run("clip.setLoopLength", {{"clip", keys.clip}, {"end", 1920 + (2 * kBar) + 960}});
    keys.rig.run("transport.locate", {{"at", 1920 + kBar + 960}});
    keys.rig.run("selection.set", {{"clips", {keys.clip}}});

    const Json split = keys.rig.run("clip.split");
    const std::string right = split.at("splits").at(0).at("right").at("id");
    CHECK(keys.midi(keys.clip).loopLength == kBar + 960);
    CHECK(keys.noteOns() == NoteOns{{0, 60}, {1920, 64}, {kBar, 60}});
    const model::MidiClip& rest = keys.midi(right);
    CHECK_FALSE(model::looped(rest));
    CHECK(rest.start == 1920 + kBar + 960);
    CHECK(rest.length == kBar);
    CHECK(keys.noteOns(1) == NoteOns{{960, 64}, {2880, 60}});
}
