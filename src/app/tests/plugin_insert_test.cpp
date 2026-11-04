#include <sndfile.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/engine/sound_file.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;
using Catch::Approx;

namespace {

const std::string kGain = "454D525447414E310000000000000001";

struct PluginInsertRig : ProjectRig {
    PluginInsertRig() {
        registerPluginActions(registry, nav, catalog);
        registerInstrumentActions(registry, *controller, nav, catalog);
        registry.setSetting(kVst3FoldersSetting, Json::array({EMERICK_TEST_VST3_FOLDER}));
    }

    // The test folder holds a bundle that hangs, so a short timeout keeps each scan quick, and one long enough that a
    // busy CI machine still sees the crashing bundle crash.
    PluginCatalog catalog{{.cacheFolder = path("cache"),
                           .scanner = EMERICK_PLUGIN_SCANNER,
                           .folders = {},
                           .timeout = std::chrono::seconds(2)}};

    // An audio track playing a constant 0.5 with the test gain as its insert.
    std::pair<std::string, std::string> guitarWithGain() {
        const std::string guitar = run("track.addAudio", {{"name", "Guitar"}}).at("id");
        writeConstant(path("dc.wav"), 48000, 0.5F);
        run("clip.import", {{"track", guitar}, {"path", path("dc.wav").string()}, {"at", "1.1.0"}});
        const std::string amp = run("insert.add", {{"track", guitar}, {"plugin", kGain}, {"format", "vst3"}}).at("id");
        return {guitar, amp};
    }

    float mixAt(core::Frames frame) {
        const std::filesystem::path mix = path("mix.wav");
        run("export.mix", {{"path", mix.string()}});
        SF_INFO info{};
        SNDFILE* file = engine::openSoundFile(mix, SFM_READ, &info);
        REQUIRE(file != nullptr);
        std::vector<float> samples(static_cast<std::size_t>(info.channels));
        sf_seek(file, frame, SEEK_SET);
        sf_readf_float(file, samples.data(), 1);
        sf_close(file);
        return samples.at(0);
    }
};

template <class Predicate>
bool turnEventsUntil(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return predicate();
}

double gainOf(const PluginInsertRig& rig, const std::string& insert) {
    return rig.run("insert.params", {{"insert", insert}}).at("params")[0].at("value").get<double>();
}

}  // namespace

TEST_CASE("plugins.list lists a VST3 effect and blocks the VST3 bundles that crash or hang") {
    PluginInsertRig rig;
    const Json listed = rig.run("plugins.list");
    const Json& plugins = listed.at("plugins");
    const auto gain = std::ranges::find_if(plugins, [](const Json& plugin) {
        return plugin.at("id") == kGain && plugin.at("bundle").get<std::string>().contains("gain");
    });
    REQUIRE(gain != plugins.end());
    CHECK(gain->at("format") == "vst3");
    CHECK(gain->at("effect") == true);
    CHECK(gain->at("instrument") == false);
    CHECK(std::ranges::any_of(
        plugins, [](const Json& plugin) { return plugin.at("bundle").get<std::string>().contains("described"); }));
    const Json& blocked = listed.at("blocked");
    CHECK(blocked.size() == 2);
    CHECK(
        std::ranges::any_of(blocked, [](const Json& entry) { return entry.at("reason") == "crashed while loading"; }));
    CHECK(listed.at("message").get<std::string>().starts_with("Found 0 CLAP and 3 VST3 plugins"));
}

TEST_CASE("a VST3 insert plays in the mix, takes params by name and choice text, and bypass passes the audio") {
    PluginInsertRig rig;
    const auto [guitar, amp] = rig.guitarWithGain();
    CHECK(rig.controller->project().undoName() == "add Test Gain");
    const Json listed = rig.run("insert.params", {{"insert", amp}});
    REQUIRE(listed.at("params").size() == 2);
    CHECK(listed.at("params")[1].at("choices") == Json::array({"Normal", "Inverted"}));
    CHECK(rig.mixAt(24000) == Approx(0.5F).margin(1e-4));

    const Json set = rig.run("insert.setParam", {{"insert", amp}, {"param", "gain"}, {"value", 0.5}});
    CHECK(set.at("param") == "Gain");
    CHECK(set.at("text") == "0.50x");
    CHECK(set.at("params").at("Gain") == 0.5);
    CHECK(rig.mixAt(24000) == Approx(0.25F).margin(1e-4));
    rig.run("insert.setParam", {{"param", amp + ".Polarity"}, {"value", "Inverted"}});
    CHECK(rig.mixAt(24000) == Approx(-0.25F).margin(1e-4));
    CHECK_THROWS_AS(rig.run("insert.setParam", {{"insert", amp}, {"param", "Drive"}, {"value", 1}}), AppError);
    CHECK_THROWS_AS(rig.run("insert.setParam", {{"insert", amp}, {"param", "Gain"}, {"value", 9}}), AppError);

    rig.run("insert.setBypass", {{"insert", amp}, {"value", true}});
    CHECK(rig.mixAt(24000) == Approx(0.5F).margin(1e-4));
    rig.run("insert.setBypass", {{"insert", amp}, {"value", false}});
    for (int step = 0; step < 3; ++step) {
        rig.run("project.undo");
    }
    rig.controller->flush();
    CHECK(rig.run("insert.params", {{"insert", amp}}).at("params")[1].at("value") == 0.0);
    CHECK(rig.mixAt(24000) == Approx(0.25F).margin(1e-4));
}

TEST_CASE("a VST3 insert's state saves with the project and loads back") {
    PluginInsertRig rig;
    const auto [guitar, amp] = rig.guitarWithGain();
    rig.run("insert.setParam", {{"insert", amp}, {"param", "Gain"}, {"value", 1.5}});
    rig.controller->save();

    PluginInsertRig other;
    other.controller->open(rig.controller->folder());
    other.controller->flush();
    const model::Track& track = *other.controller->project().track(model::TrackId{guitar});
    REQUIRE(track.inserts.size() == 1);
    REQUIRE(track.inserts.front().plugin.has_value());
    CHECK(track.inserts.front().plugin->format == core::PluginFormat::vst3);
    const auto plugin = other.controller->instruments().find(model::InsertId{amp});
    REQUIRE(plugin != nullptr);
    CHECK(plugin->paramValue(1) == 1.5);
}

TEST_CASE("a VST3 insert param is an automation param whose lane moves the plugin") {
    PluginInsertRig rig;
    const auto [guitar, amp] = rig.guitarWithGain();
    rig.run("automation.addPoint", {{"track", guitar}, {"param", amp + ".Gain"}, {"tick", 0}, {"value", 0.5}});
    const model::Track& track = *rig.controller->project().track(model::TrackId{guitar});
    const auto lanes = pluginLanes(track, rig.controller->instruments().insertPlugins());
    REQUIRE(lanes.size() == 1);
    CHECK(lanes.front().param == 1);
    CHECK(lanes.front().points.front().value == 0.25F);
    CHECK(rig.mixAt(24000) == Approx(0.25F).margin(1e-4));
}

TEST_CASE("a missing VST3 insert stays in the project, warns on its track and stops an export") {
    PluginInsertRig rig;
    const auto [guitar, amp] = rig.guitarWithGain();
    rig.controller->project().editTrack(model::TrackId{guitar}, "move plugin", [](model::Track& edited) {
        edited.inserts.front().plugin->bundle = "/no/such/Gain.vst3";
    });
    rig.controller->flush();
    CHECK(
        rig.controller->trackErrors().value(QString::fromStdString(guitar)).toString().startsWith("Install Test Gain"));
    CHECK_THROWS_AS(rig.run("insert.params", {{"insert", amp}}), AppError);
    CHECK_THROWS_AS(rig.mixAt(24000), AppError);
    REQUIRE(rig.controller->project().track(model::TrackId{guitar})->inserts.front().plugin.has_value());
}

TEST_CASE("a VST3 insert's window opens at its editor's size, and a gesture in the editor lands as one undo step") {
    PluginInsertRig rig;
    const auto [guitar, amp] = rig.guitarWithGain();
    CHECK_THROWS_AS(rig.run("insert.openPluginWindow", {{"insert", amp + "x"}}), AppError);
    const Json opened = rig.run("insert.openPluginWindow", {{"insert", amp}});
    CHECK(opened.at("windowOpen") == true);
    REQUIRE(turnEventsUntil([&] { return rig.controller->project().undoName() == "edit Test Gain"; }));
    CHECK(rig.controller->instruments().editorOpen(model::InsertId{amp}));
    CHECK(gainOf(rig, amp) == 0.25);
    CHECK(rig.mixAt(24000) == Approx(0.125F).margin(1e-4));
    rig.run("project.undo");
    rig.controller->flush();
    CHECK(gainOf(rig, amp) == 1.0);
    CHECK(rig.controller->instruments().editorOpen(model::InsertId{amp}));
    rig.run("project.redo");
    rig.controller->flush();
    CHECK(gainOf(rig, amp) == 0.25);
    CHECK(rig.run("insert.closePluginWindow", {{"insert", amp}}).at("windowOpen") == false);
    CHECK_FALSE(rig.controller->instruments().editorOpen(model::InsertId{amp}));
}

TEST_CASE("a built-in insert has no plugin window") {
    PluginInsertRig rig;
    const std::string vox = rig.run("track.addAudio").at("id");
    const std::string eq = rig.run("insert.add", {{"track", vox}, {"type", "eq"}}).at("id");
    CHECK_THROWS_AS(rig.run("insert.openPluginWindow", {{"insert", eq}}), AppError);
}

namespace {

const std::string kSynth = "454D525453594E310000000000000001";

float mixPeak(PluginInsertRig& rig) {
    const std::filesystem::path mix = rig.path("synth.wav");
    rig.run("export.mix", {{"path", mix.string()}});
    SF_INFO info{};
    SNDFILE* file = engine::openSoundFile(mix, SFM_READ, &info);
    REQUIRE(file != nullptr);
    std::vector<float> samples(static_cast<std::size_t>(info.frames * info.channels));
    sf_readf_float(file, samples.data(), info.frames);
    sf_close(file);
    return std::ranges::max(samples | std::views::transform([](float sample) { return std::abs(sample); }));
}

double levelOf(const PluginInsertRig& rig, const model::TrackId& track) {
    return rig.run("instrument.params", {{"track", track.value}}).at("params")[0].at("value").get<double>();
}

}  // namespace

TEST_CASE("a VST3 instrument plays a MIDI clip, and a gesture in its editor lands as one undo step") {
    PluginInsertRig rig;
    const model::TrackId track{
        rig.run("track.addInstrument", {{"plugin", kSynth}, {"format", "vst3"}}).at("id").get<std::string>()};
    REQUIRE(rig.controller->project().track(track)->instrument->format == core::PluginFormat::vst3);
    rig.controller->project().editTrack(track, "add note", [](model::Track& edited) {
        model::MidiClip clip;
        clip.length = 960;
        clip.notes = {{.pitch = 69, .velocity = 127, .start = 0, .length = 480, .channel = 1}};
        edited.clips.emplace_back(clip);
    });
    rig.controller->flush();
    CHECK(mixPeak(rig) == Approx(0.5F).margin(0.02));

    rig.run("instrument.showEditor", {{"track", track.value}});
    REQUIRE(turnEventsUntil([&] { return rig.controller->project().undoName() == "edit Test Synth"; }));
    CHECK(levelOf(rig, track) == 0.25);
    CHECK(mixPeak(rig) == Approx(0.25F).margin(0.02));
    rig.run("project.undo");
    rig.controller->flush();
    CHECK(levelOf(rig, track) == 0.5);
}

TEST_CASE("a CLAP effect plays as an insert, takes params by name and automation, bypasses and keeps its state") {
    PluginInsertRig rig;
    rig.registry.setSetting(kClapFoldersSetting, Json::array({EMERICK_TEST_CLAP_EFFECT_FOLDER}));
    const Json listed = rig.run("plugins.rescan");
    const auto gain = std::ranges::find(listed.at("plugins"), Json("org.emerick.test.gain"),
                                        [](const Json& plugin) { return plugin.at("id"); });
    REQUIRE(gain != listed.at("plugins").end());
    CHECK(gain->at("format") == "clap");
    CHECK(gain->at("effect") == true);

    const std::string guitar = rig.run("track.addAudio", {{"name", "Guitar"}}).at("id");
    writeConstant(rig.path("dc.wav"), 48000, 0.5F);
    rig.run("clip.import", {{"track", guitar}, {"path", rig.path("dc.wav").string()}, {"at", "1.1.0"}});
    const std::string amp = rig.run("insert.add", {{"track", guitar}, {"plugin", "org.emerick.test.gain"}}).at("id");
    CHECK(rig.run("insert.params", {{"insert", amp}}).at("params")[0].at("name") == "Gain");
    CHECK(rig.mixAt(24000) == Approx(0.5F).margin(1e-4));
    CHECK(rig.run("insert.setParam", {{"insert", amp}, {"param", "gain"}, {"value", 0.5}}).at("text") == "0.50x");
    CHECK(rig.mixAt(24000) == Approx(0.25F).margin(1e-4));
    rig.run("insert.setBypass", {{"insert", amp}, {"value", true}});
    CHECK(rig.mixAt(24000) == Approx(0.5F).margin(1e-4));
    rig.run("insert.setBypass", {{"insert", amp}, {"value", false}});
    rig.run("automation.addPoint", {{"track", guitar}, {"param", amp + ".Gain"}, {"tick", 0}, {"value", 1.5}});
    CHECK(rig.mixAt(24000) == Approx(0.75F).margin(1e-4));
    rig.controller->save();

    PluginInsertRig other;
    other.controller->open(rig.controller->folder());
    other.controller->flush();
    const auto plugin = other.controller->instruments().find(model::InsertId{amp});
    REQUIRE(plugin != nullptr);
    CHECK(plugin->paramValue(0) == 0.5);
}

TEST_CASE("a plugin insert's latency delays the other tracks, and the stream state shows the compensation") {
    PluginInsertRig rig;
    rig.registry.setSetting(kClapFoldersSetting, Json::array({EMERICK_TEST_CLAP_EFFECT_FOLDER}));
    rig.run("plugins.rescan");
    std::vector<float> click(48000);
    click[1000] = 1.0F;
    const std::filesystem::path clip = rig.path("click.wav");
    SF_INFO info{.frames = 0,
                 .samplerate = 48000,
                 .channels = 1,
                 .format = SF_FORMAT_WAV | SF_FORMAT_FLOAT,
                 .sections = 0,
                 .seekable = 0};
    SNDFILE* file = engine::openSoundFile(clip, SFM_WRITE, &info);
    REQUIRE(file != nullptr);
    sf_writef_float(file, click.data(), static_cast<sf_count_t>(click.size()));
    sf_close(file);
    for (const char* name : {"Slow", "Fast"}) {
        const std::string track = rig.run("track.addAudio", {{"name", name}}).at("id");
        rig.run("clip.import", {{"track", track}, {"path", clip.string()}, {"at", "1.1.0"}});
        if (std::string_view{name} == "Slow") {
            rig.run("insert.add", {{"track", track}, {"plugin", "org.emerick.test.late"}});
        }
    }
    rig.controller->flush();
    CHECK(rig.registry.allState().at("transport").at("stream").at("pluginDelayFrames") == 480);
    rig.run("export.stems", {{"folder", rig.path("stems").string()}});
    const auto peakFrame = [&](const std::string& stem) {
        SF_INFO read{};
        SNDFILE* stemFile = engine::openSoundFile((rig.path("stems") / stem), SFM_READ, &read);
        REQUIRE(stemFile != nullptr);
        std::vector<float> samples(static_cast<std::size_t>(read.frames * read.channels));
        sf_readf_float(stemFile, samples.data(), read.frames);
        sf_close(stemFile);
        return (std::ranges::max_element(samples, {}, [](float sample) { return std::abs(sample); }) -
                samples.begin()) /
               read.channels;
    };
    CHECK(peakFrame("Slow.wav") == peakFrame("Fast.wav"));
}
