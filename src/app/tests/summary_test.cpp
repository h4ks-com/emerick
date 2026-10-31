#include <QTimer>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <string>
#include <vector>

#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

constexpr core::Ticks kBar = 3840;

struct KeysRig {
    ProjectRig rig;
    std::string track = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    std::string clip = rig.run("midi.addClip", {{"track", track}, {"at", kBar}, {"length", 2 * kBar}}).at("id");

    void add(int pitch, core::Ticks start, int velocity, int channel = 1) const {
        rig.run("midi.addNote", {{"clip", clip},
                                 {"pitch", pitch},
                                 {"start", start},
                                 {"length", 960},
                                 {"velocity", velocity},
                                 {"channel", channel}});
    }
};

}  // namespace

TEST_CASE("analyzing a MIDI clip reports its range, velocities, bars, polyphony, channels and controllers") {
    KeysRig keys;
    keys.add(60, 0, 80);
    keys.add(64, 0, 100);
    keys.add(67, 480, 120, 2);
    keys.add(72, kBar, 90);
    keys.rig.run(
        "midi.setControl",
        {{"clip", keys.clip}, {"lane", "cc64"}, {"points", {{{"at", 0}, {"value", 127}}, {{"at", 10}, {"value", 0}}}}});

    const Json clips = keys.rig.run("midi.analyze", {{"clip", keys.clip}}).at("clips");
    REQUIRE(clips.size() == 1);
    const Json& clip = clips.at(0);
    CHECK(clip.at("track") == keys.track);
    CHECK(clip.at("startBar") == 2);
    CHECK(clip.at("lengthBars") == 2.0);
    CHECK(clip.at("noteCount") == 4);
    CHECK(clip.at("lowest") == Json{{"pitch", 60}, {"name", "C3"}});
    CHECK(clip.at("highest") == Json{{"pitch", 72}, {"name", "C4"}});
    CHECK(clip.at("velocity") == Json{{"min", 80}, {"mean", 97.5}, {"max", 120}});
    CHECK(clip.at("notesPerBar") == Json{{"2", 3}, {"3", 1}});
    CHECK(clip.at("maxPolyphony") == 3);
    CHECK(clip.at("channels") == Json{1, 2});
    CHECK(clip.at("controls") == Json{{"cc64", {{"min", 0}, {"max", 127}, {"points", 2}}}});
}

TEST_CASE("analyzing without a clip covers a track's MIDI clips or every MIDI clip, and an audio clip is refused") {
    KeysRig keys;
    const std::string bass = keys.rig.run("track.addMidi", {{"name", "Bass"}}).at("id");
    keys.rig.run("midi.addClip", {{"track", bass}, {"at", 0}});

    CHECK(keys.rig.run("midi.analyze", {{"track", "Keys"}}).at("clips").size() == 1);
    CHECK(keys.rig.run("midi.analyze").at("clips").size() == 2);
    const Json empty = keys.rig.run("midi.analyze", {{"track", bass}}).at("clips").at(0);
    CHECK(empty.at("noteCount") == 0);
    CHECK_FALSE(empty.contains("lowest"));
}

TEST_CASE("the project summary lists tempo, length, position and each track's mix and contents") {
    KeysRig keys;
    keys.add(60, 0, 100);
    keys.add(64, 0, 100);
    const std::string vox = keys.rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    const std::string verb = keys.rig.run("track.addBus", {{"name", "Verb"}}).at("id");
    keys.rig.run("insert.add", {{"track", vox}, {"type", "eq"}});
    keys.rig.run("send.add", {{"track", vox}, {"bus", verb}, {"db", -6.0}});
    keys.rig.run("project.setTempo", {{"bpm", 96}, {"numerator", 3}, {"denominator", 4}});

    const Json summary = keys.rig.run("project.summary");
    CHECK(summary.at("tempo") == Json{{"bpm", 96.0}, {"numerator", 3}, {"denominator", 4}});
    CHECK(summary.at("position") == "1.1.0");
    CHECK(summary.at("lengthBars").get<int>() >= 3);
    CHECK(summary.at("issues").is_array());
    const Json& tracks = summary.at("tracks");
    REQUIRE(tracks.size() == 3);
    CHECK(tracks.at(0).at("kind") == "midi");
    CHECK(tracks.at(0).at("clips") == 1);
    CHECK(tracks.at(0).at("notes") == 2);
    CHECK(tracks.at(1).at("inserts") == Json{{{"name", "Channel EQ"}, {"bypass", false}}});
    CHECK(tracks.at(1).at("sends") == Json{{{"bus", verb}, {"db", -6.0}, {"enabled", true}}});
    CHECK_FALSE(tracks.at(1).contains("instrument"));
    CHECK(summary.at("ready") == true);
}

TEST_CASE("waiting for instruments returns once none still loads, and fails naming them after its timeout") {
    ProjectRig rig;
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    bool loading = true;
    rig.registry.addLoadingTracks([&](const model::Document&) {
        return loading ? std::vector{model::TrackId{keys}} : std::vector<model::TrackId>{};
    });

    CHECK(rig.run("project.summary").at("ready") == false);
    CHECK_THROWS_WITH(rig.run("project.waitReady", {{"timeout", 0.05}}),
                      Catch::Matchers::ContainsSubstring("Keys still loading"));
    CHECK_THROWS_AS(rig.run("project.waitReady", {{"timeout", -1}}), AppError);

    QTimer::singleShot(20, [&] { loading = false; });
    CHECK(rig.run("project.waitReady", {{"timeout", 5}}).at("ready") == true);
    CHECK(rig.run("project.summary").at("ready") == true);
}
