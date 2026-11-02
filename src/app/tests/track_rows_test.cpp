#include "emerick/app/track_rows.hpp"

#include <catch2/catch_test_macros.hpp>
#include <ranges>

using namespace emerick;

namespace {

constexpr core::Ticks kBar = 4 * core::kTicksPerQuarter;
constexpr double kRate = 48000.0;

const app::RowContext kContext{.tempo = {}, .sampleRate = kRate, .folder = "/songs/Demo.erk"};

}  // namespace

TEST_CASE("a MIDI clip row lists its notes, times and loop") {
    model::MidiClip clip{.start = kBar, .length = kBar, .name = "Riff"};
    clip.notes.push_back(model::Note{.pitch = 64, .velocity = 90, .start = 240, .length = 480});
    const QVariantMap entry = app::clipEntry(clip, 3, kContext);
    CHECK(entry["type"] == "midi");
    CHECK(entry["take"] == 3);
    CHECK(entry["name"] == "Riff");
    CHECK(entry["startText"] == "2.1.0");
    CHECK(entry["lengthText"] == "1.0.0");
    CHECK(entry["looped"] == false);
    const QVariantMap note = entry["notes"].toList().front().toMap();
    CHECK(note["pitch"] == 64);
    CHECK(note["velocity"] == 90);
    CHECK(note["start"] == 240);
    CHECK(app::noteList(clip, kBar).front().toMap()["start"].toLongLong() == kBar + 240);
}

TEST_CASE("an audio clip row counts its frames at the context's rate and finds its file from the bundle") {
    const model::AudioClip clip{.start = 0, .length = kBar, .file = "audio/take.wav", .fadeIn = 120};
    const QVariantMap entry = app::clipEntry(clip, 1, kContext);
    CHECK(entry["type"] == "audio");
    CHECK(entry["frames"] == static_cast<qint64>(2 * kRate));
    CHECK(entry["file"] == "/songs/Demo.erk/audio/take.wav");
    CHECK(entry["fadeIn"] == 120);
    CHECK(entry["fadeInShape"] == "linear");
    const model::AudioClip outside{.start = 0, .length = kBar, .file = "/samples/kick.wav"};
    CHECK(app::clipEntry(outside, 1, kContext)["file"] == "/samples/kick.wav");
}

TEST_CASE("a track's params list its gain, pan, mute, sends and built-in insert params with their ranges") {
    model::Document document;
    model::Track bus{.name = "Room", .kind = "bus"};
    model::Track vox{.name = "Vox", .kind = "audio", .gainDb = -3.0};
    vox.sends.push_back(model::Send{.bus = bus.id, .levelDb = -6.0});
    vox.inserts.push_back(model::Insert{.type = "eq"});
    document.tracks = {vox, bus};

    const auto params = app::trackParams(document, document.tracks.front());
    const auto ids = params | std::views::transform(&app::AutomationParam::id);
    const std::string eq = vox.inserts.front().id.value;
    CHECK(std::ranges::contains(ids, "send." + bus.id.value + ".level"));
    CHECK(std::ranges::contains(ids, eq + ".peak2Gain"));
    CHECK_FALSE(std::ranges::contains(ids, eq + ".lowCutSlope"));

    const QVariantList rows = app::paramList(params);
    const QVariantMap gain = rows.front().toMap();
    CHECK(gain["id"] == "gain");
    CHECK(gain["value"] == -3.0);
    CHECK(gain["scale"] == "fader");
    CHECK(rows.at(3).toMap()["title"] == "Room send");
}

TEST_CASE("a track's lanes list each point with its curve") {
    model::Track track;
    track.automation.push_back(model::AutomationLane{
        .param = "pan",
        .points = {{.tick = 0, .value = -1.0}, {.tick = kBar, .value = 1.0, .curve = core::AutomationCurve::hold}}});
    const QVariantMap lane = app::laneList(track).front().toMap();
    CHECK(lane["param"] == "pan");
    const QVariantList points = lane["points"].toList();
    REQUIRE(points.size() == 2);
    CHECK(points.at(1).toMap()["tick"].toLongLong() == kBar);
    CHECK(points.at(1).toMap()["curve"] == "hold");
}

TEST_CASE("a track's mixer rows name its instrument's sound, its inserts and its sends") {
    model::Document document;
    model::Track bus{.name = "Room", .kind = "bus"};
    const std::string gm = R"({"sound": 33})";
    const std::string voices = R"({"voice": {"pack": "Jazz", "name": "Upright"}})";
    model::Track keys{.name = "Keys", .kind = "midi"};
    keys.instrument = model::PluginSlot{
        .pluginId = app::kGeneralMidiPluginId, .name = "General MIDI", .state = {gm.begin(), gm.end()}};
    keys.inserts.push_back(model::Insert{.type = "reverb"});
    keys.inserts.push_back(model::Insert{
        .type = model::kPluginInsertType,
        .plugin =
            model::PluginSlot{.pluginId = "com.example.verb", .name = "Big Verb", .format = core::PluginFormat::vst3}});
    keys.sends.push_back(model::Send{.bus = bus.id, .levelDb = -9.0});
    document.tracks = {keys, bus};

    const QVariantMap instrument = app::instrumentEntry(keys);
    CHECK(instrument["name"] == "General MIDI");
    CHECK(instrument["format"] == "CLAP");
    CHECK(instrument["sound"] == "Electric Bass (finger)");
    model::Track upright{.name = "Upright", .kind = "midi"};
    upright.instrument = model::PluginSlot{
        .pluginId = app::kVoicesPluginId, .name = "Expansion Voices", .state = {voices.begin(), voices.end()}};
    CHECK(app::instrumentEntry(upright)["sound"] == "Jazz / Upright");
    CHECK(app::instrumentEntry(bus).isEmpty());

    const QVariantList inserts = app::insertList(keys);
    CHECK(inserts.at(0).toMap()["title"] == "Reverb");
    CHECK(inserts.at(1).toMap()["title"] == "Big Verb");
    CHECK(inserts.at(1).toMap()["plugin"].toMap()["format"] == "vst3");
    const QVariantMap send = app::sendList(keys, document, &model::Send::levelDb).front().toMap();
    CHECK(send["busName"] == "Room");
    CHECK(send["db"] == -9.0);
}
