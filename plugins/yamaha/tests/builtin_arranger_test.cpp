#include <sndfile.h>

#include <QByteArray>
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <span>
#include <string>
#include <vector>

#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "emerick/app/session_mapping.hpp"
#include "registration.hpp"
#include "style_builder.hpp"
#include "style_library.hpp"
#include "synthetic_store.hpp"
#include "yamaha_rig.hpp"

using namespace emerick;
using namespace emerick::yamaha::test;
using app::AppError;

namespace {

const std::vector<std::string> kPartNames{"Rhythm 1", "Rhythm 2", "Bass",     "Chord 1",
                                          "Chord 2",  "Pad",      "Phrase 1", "Phrase 2"};

// Expansion Voices reads its store under HOME, so we point HOME at a folder of our own while a test runs.
class HomeFolder {
public:
    explicit HomeFolder(const std::filesystem::path& folder) : saved_(qgetenv("HOME")) {
        std::filesystem::create_directories(folder);
        qputenv("HOME", QByteArray::fromStdString(folder.string()));
    }
    ~HomeFolder() { qputenv("HOME", saved_); }
    HomeFolder(const HomeFolder&) = delete;
    HomeFolder& operator=(const HomeFolder&) = delete;
    HomeFolder(HomeFolder&&) = delete;
    HomeFolder& operator=(HomeFolder&&) = delete;

private:
    QByteArray saved_;
};

struct ArrangerRig : YamahaRig {
    HomeFolder home{path("home")};
    app::PluginCatalog catalog{{.cacheFolder = path("cache"),
                                .scanner = EMERICK_CLAP_SCANNER,
                                .folders = {},
                                .timeout = std::chrono::seconds(10)}};
    std::filesystem::path styles = path("Styles");
    std::filesystem::path demo = styles / "Pop" / "Demo.sty";

    ArrangerRig() {
        app::registerPluginActions(registry, nav, catalog);
        app::registerInstrumentActions(registry, *controller, nav, catalog);
        registry.setSetting(app::kClapFoldersSetting,
                            Json::array({app::bundledClapFolder(EMERICK_GM_APP_FOLDER).string()}));
        std::filesystem::create_directories(demo.parent_path());
        const Bytes bytes = arranger::test::demoStyle();
        std::ofstream(demo, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        std::ofstream(styles / "Notes.txt") << "not a style";
        registry.setSetting("yamaha.styleFolders", Json::array({styles.string()}));
    }

    [[nodiscard]] const model::Document& document() const { return controller->project().document(); }

    // Runs the action, then the event loop until Expansion Voices read its store and the parts waiting for it settled.
    Json run(const std::string& id, const Json& params = Json::object()) const {
        Json result = YamahaRig::run(id, params);
        REQUIRE(processUntil([&] { return !voicesLoading(); }, 10000));
        return result;
    }

    [[nodiscard]] bool voicesLoading() const {
        return std::ranges::any_of(document().tracks, [&](const model::Track& track) {
            if (!track.instrument || track.instrument->pluginId != yamaha::kVoicesPluginId) {
                return false;
            }
            const Json described = controller->instruments().describe(track.id);
            return !described.is_null() && described.at("params").at(0).at("text") == "Loading voices";
        });
    }

    [[nodiscard]] std::string trackOfKind(const char* kind) const {
        const auto found = std::ranges::find(document().tracks, std::string{kind}, &model::Track::kind);
        REQUIRE(found != document().tracks.end());
        return found->id.value;
    }

    [[nodiscard]] Json arrangerState() const {
        const auto& state = track(trackOfKind("yamaha.style")).instrument->state;
        return Json::parse(state);
    }
};

// The demo style with the last byte of the first match of message replaced by value.
std::filesystem::path patchedStyle(const ArrangerRig& rig, const std::string& name,
                                   std::span<const std::uint8_t> message, std::uint8_t value) {
    Bytes bytes = arranger::test::demoStyle();
    const auto found = std::ranges::search(bytes, message);
    REQUIRE_FALSE(found.empty());
    found.back() = value;
    const std::filesystem::path path = rig.path(name);
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

// The demo style with its bass on another program, which no store in these tests holds.
std::filesystem::path otherBassStyle(const ArrangerRig& rig) {
    return patchedStyle(rig, "Other.sty", std::array<std::uint8_t, 2>{0xC0 | arranger::test::kBass, 33}, 34);
}

float loudest(const std::filesystem::path& file) {
    SF_INFO info{};
    SNDFILE* opened = sf_open(file.c_str(), SFM_READ, &info);
    REQUIRE(opened != nullptr);
    std::vector<float> samples(static_cast<std::size_t>(info.frames * info.channels));
    sf_readf_float(opened, samples.data(), info.frames);
    sf_close(opened);
    return std::ranges::max(samples | std::views::transform([](float sample) { return std::abs(sample); }));
}

}  // namespace

TEST_CASE("choosing a style switches the part tracks it has voices for between Expansion Voices and General MIDI") {
    ArrangerRig rig;
    voices::test::SyntheticStore store;
    const std::string wave = store.addWave(voices::test::linearBlob(voices::test::sine(440.0, 44100.0, 441), 44100.0));
    store.addPack("Pack", {voices::test::SyntheticVoice{
                              .name = "Bass", .msb = 0, .lsb = 0, .program = 33, .zones = {{.wave = wave}}}});
    const std::filesystem::path packs = rig.path("home") / "Library/Application Support/Yamaha/Expansion Manager/Packs";
    std::filesystem::create_directories(packs.parent_path());
    std::filesystem::create_directory_symlink(store.folder(), packs);

    rig.run("arranger.setup");
    const std::string style = rig.trackOfKind("yamaha.style");
    const auto part = [&](std::size_t index) { return rig.document().tracks.at(index + 2).id.value; };
    const auto plugin = [&](std::size_t index) {
        const auto& instrument = rig.track(part(index)).instrument;
        return instrument ? instrument->pluginId : std::string{};
    };
    rig.run("instrument.remove", {{"track", part(3)}});

    rig.run("arranger.style.load", {{"track", style}, {"path", rig.demo.string()}});
#ifdef __APPLE__
    const std::string bassPlugin = "org.emerick.voices";
    REQUIRE(plugin(2) == bassPlugin);
    CHECK(Json::parse(rig.track(part(2)).instrument->state).at("followProgramChanges") == true);
#else
    const std::string bassPlugin = "org.emerick.gm";
#endif
    CHECK(plugin(0) == "org.emerick.gm");
    CHECK(plugin(1) == "org.emerick.gm");
    CHECK(plugin(2) == bassPlugin);
    CHECK(plugin(3).empty());

    // The parts whose voices the store lacks switch to General MIDI in a step of their own once the store is read.
    rig.run("project.undo");
    CHECK(rig.controller->project().redoName() == "play general midi for missing voices");
    rig.run("project.undo");
    CHECK(rig.track(style).instrument->state.empty());
    CHECK(plugin(2) == "org.emerick.gm");

    rig.run("project.redo");
    CHECK(plugin(2) == "org.emerick.voices");
    rig.run("arranger.style.load", {{"track", style}, {"path", otherBassStyle(rig).string()}});
    CHECK(plugin(2) == "org.emerick.gm");
    CHECK(plugin(3).empty());
}

TEST_CASE("the Expansion Voices folder setting gives instances whose state names no folder its store") {
    ArrangerRig rig;
    voices::test::SyntheticStore store;
    const std::string wave = store.addWave(voices::test::linearBlob(voices::test::sine(440.0, 44100.0, 441), 44100.0));
    store.addPack("Pack", {voices::test::SyntheticVoice{.name = "Bass", .program = 33, .zones = {{.wave = wave}}}});
    rig.run("track.addMidi");
    const model::TrackId track = rig.document().tracks.back().id;
    rig.run("instrument.load", {{"track", track.value}, {"plugin", yamaha::kVoicesPluginId}});
    rig.controller->flush();
    const auto choices = [&] { return rig.controller->instruments().describe(track).at("params").at(0).at("choices"); };
    REQUIRE(processUntil([&] { return choices() == Json::array({"None"}); }, 10000));

    rig.registry.setSetting(yamaha::kVoicesFolderSetting, store.folder().string());
    REQUIRE(processUntil([&] { return choices().size() == 2; }, 10000));
    CHECK(choices().at(1) == "Pack / Bass");

    rig.registry.setSetting(yamaha::kVoicesFolderSetting, "");
    REQUIRE(processUntil([&] { return choices() == Json::array({"None"}); }, 10000));
}

TEST_CASE("a part without an installed voice plays its part's General MIDI default and warns which pack it needs") {
    ArrangerRig rig;
    rig.run("arranger.setup");
    const std::string style = rig.trackOfKind("yamaha.style");
    rig.run("arranger.style.load", {{"track", style}, {"path", rig.demo.string()}});
    rig.controller->flush();
    const auto part = [&](std::size_t index) { return rig.document().tracks.at(index + 2).id.value; };
    const auto param = [&](std::size_t index, std::size_t id) {
        return rig.run("instrument.params", {{"track", part(index)}}).at("params")[id].at("value");
    };
    const auto warning = [&](std::size_t index) {
        return rig.controller->trackErrors().value(QString::fromStdString(part(index))).toString().toStdString();
    };
    CHECK(param(2, 0) == 33);
    CHECK(param(2, 2) == 1);
    CHECK(param(3, 0) == 27);
    CHECK(param(3, 2) == 0);
    CHECK(param(0, 0) == 0);
    CHECK(param(0, 2) == 1);
    CHECK(param(3, 3) == 95);
    CHECK(param(2, 3) == 127);
    CHECK(param(0, 3) == 127);
    CHECK(warning(2) ==
          "Install the pack with voice 0/0/34 and choose Expansion Voices to hear it in place of General MIDI");
    CHECK(warning(3) ==
          "Install the pack with voice 8/2/26 and choose Expansion Voices to hear it in place of General MIDI");
    CHECK(warning(0) ==
          "Install the pack with voice 127/0/1 and choose Expansion Voices to hear it in place of General MIDI");
    CHECK(warning(5).empty());

    rig.run("instrument.remove", {{"track", part(3)}});
    rig.controller->flush();
    CHECK(warning(3).empty());
}

TEST_CASE("a drum part's General MIDI picks the kit the style's program names, and Standard for any other program") {
    ArrangerRig rig;
    rig.run("arranger.setup");
    const std::string style = rig.trackOfKind("yamaha.style");
    const auto drums = [&](std::size_t id) {
        return rig.run("instrument.params", {{"track", rig.document().tracks.at(2).id.value}})
            .at("params")[id]
            .at("value");
    };
    const std::array<std::uint8_t, 2> drumProgram{0xC0 | arranger::test::kDrums, 0};

    rig.run("arranger.style.load",
            {{"track", style}, {"path", patchedStyle(rig, "Kit.sty", drumProgram, 25).string()}});
    CHECK(drums(1) == 4);
    CHECK(drums(2) == 1);
    rig.run("arranger.style.load",
            {{"track", style}, {"path", patchedStyle(rig, "NoKit.sty", drumProgram, 100).string()}});
    CHECK(drums(1) == 0);
}

TEST_CASE("the voice list's note rows give a drum part's General MIDI a drum map, and a kit without rows gets none") {
    ArrangerRig rig;
    const std::filesystem::path mapped = rig.path("mapped.csv");
    std::ofstream(mapped) << "127,0,1,29,38\n127, 0, 1, 30, -1\n127,0,1,36,36\n127,0,1,200,36\n127,0,2,40,41\n";
    const std::filesystem::path other = rig.path("other.csv");
    std::ofstream(other) << "127,0,2,29,38\n";
    rig.run("arranger.setup");
    const std::string style = rig.trackOfKind("yamaha.style");
    const auto drumState = [&] {
        return Json::parse(rig.track(rig.document().tracks.at(2).id.value).instrument->state);
    };

    rig.registry.setSetting("yamaha.voiceList", mapped.string());
    rig.run("arranger.style.load", {{"track", style}, {"path", rig.demo.string()}});
    std::vector<int> expected(128, -1);
    expected.at(29) = 38;
    expected.at(36) = 36;
    CHECK(drumState().at("drumMap") == expected);

    rig.registry.setSetting("yamaha.voiceList", other.string());
    rig.run("arranger.style.load", {{"track", style}, {"path", rig.demo.string()}});
    CHECK_FALSE(drumState().contains("drumMap"));
}

TEST_CASE("the voice list picks the General MIDI family that stands in for a part's voice") {
    ArrangerRig rig;
    const std::filesystem::path inside = rig.path("inside.csv");
    std::ofstream(inside) << "msb,lsb,program,category\n8,2,26,A.Guitar\r\n0,0,34,Nothing\n8,2\n";
    const std::filesystem::path outside = rig.path("outside.csv");
    std::ofstream(outside) << " 8, 2, 26, Bass\n";
    rig.run("arranger.setup");
    const std::string style = rig.trackOfKind("yamaha.style");
    const auto part = [&](std::size_t index) { return rig.document().tracks.at(index + 2).id.value; };
    const auto param = [&](std::size_t index, std::size_t id) {
        return rig.run("instrument.params", {{"track", part(index)}}).at("params")[id].at("value");
    };

    rig.registry.setSetting("yamaha.voiceList", inside.string());
    rig.run("arranger.style.load", {{"track", style}, {"path", rig.demo.string()}});
    CHECK(param(3, 0) == 25);
    CHECK(param(3, 2) == 0);
    CHECK(param(2, 0) == 33);
    CHECK(param(2, 2) == 1);
    CHECK(param(0, 2) == 1);

    rig.registry.setSetting("yamaha.voiceList", outside.string());
    rig.run("arranger.style.load", {{"track", style}, {"path", rig.demo.string()}});
    CHECK(param(3, 0) == 33);
    CHECK(param(3, 2) == 0);
}

TEST_CASE("a rhythm part on a melodic bank stands in with a percussion sound unless the voice list names its family") {
    ArrangerRig rig;
    const std::filesystem::path list = rig.path("list.csv");
    std::ofstream(list) << "8,0,1,Bass\n";
    rig.run("arranger.setup");
    const std::string style = rig.trackOfKind("yamaha.style");
    const auto rhythm = [&](std::size_t id) {
        return rig.run("instrument.params", {{"track", rig.document().tracks.at(2).id.value}})
            .at("params")[id]
            .at("value");
    };
    const std::filesystem::path melodic =
        patchedStyle(rig, "Melodic.sty", std::array<std::uint8_t, 3>{0xB0 | arranger::test::kDrums, 0, 127}, 8);

    rig.run("arranger.style.load", {{"track", style}, {"path", melodic.string()}});
    CHECK(rhythm(0) == 11);
    CHECK(rhythm(2) == 0);
    rig.registry.setSetting("yamaha.voiceList", list.string());
    rig.run("arranger.style.load", {{"track", style}, {"path", melodic.string()}});
    CHECK(rhythm(0) == 33);
    CHECK(rhythm(2) == 0);
}

TEST_CASE("set up built-in arranger adds the Chords and Style tracks, the Arranger and a track per part in one step") {
    ArrangerRig rig;
    rig.run("arranger.setup");
    const model::Document& document = rig.document();
    REQUIRE(document.tracks.size() == 10);
    const model::Track& style = rig.track(rig.trackOfKind("yamaha.style"));
    REQUIRE(style.instrument.has_value());
    CHECK(style.instrument->pluginId == "org.emerick.arranger");
    for (std::size_t part = 0; part < kPartNames.size(); ++part) {
        const model::Track& track = document.tracks.at(part + 2);
        CHECK(track.name == kPartNames[part]);
        CHECK(track.midiIn.sourceTrack == style.id);
        CHECK(track.midiIn.channel == static_cast<int>(part) + 9);
        CHECK_FALSE(track.armed);
        REQUIRE(track.instrument.has_value());
        CHECK(track.instrument->pluginId == "org.emerick.gm");
    }

    rig.run("arranger.setup");
    CHECK(rig.document().tracks.size() == 10);
    rig.run("project.undo");
    CHECK(rig.document().tracks.empty());
}

TEST_CASE("the style list reads the style folders, and choosing a style loads it into the Arranger as one step") {
    ArrangerRig rig;
    const Json styles = rig.run("arranger.style.list").at("styles");
    REQUIRE(styles.size() == 1);
    CHECK(styles[0].at("name") == "Demo");
    CHECK(styles[0].at("bpm") == 100.0);
    CHECK(styles[0].at("meter") == "4/4");
    CHECK(styles[0].at("sections") == Json::array({"I1", "A", "B", "Fill", "Break", "E1"}));

    rig.run("arranger.setup");
    const std::string style = rig.trackOfKind("yamaha.style");
    CHECK_THROWS_AS(rig.run("arranger.style.load", {{"track", style}, {"path", (rig.styles / "Notes.txt").string()}}),
                    AppError);
    CHECK_THROWS_AS(
        rig.run("arranger.style.load", {{"track", rig.trackOfKind("yamaha.chord")}, {"path", rig.demo.string()}}),
        AppError);

    const Json loaded = rig.run("arranger.style.load", {{"track", style}, {"path", rig.demo.string()}});
    CHECK(loaded.at("name") == "Demo");
    CHECK(rig.arrangerState().at("style") == rig.demo.string());
    CHECK(rig.arrangerState().at("hash").get<std::string>().size() == 16);
    CHECK(rig.arrangerState().at("autoFill") == true);
    rig.run("project.undo");
    CHECK(rig.controller->project().redoName() == "play general midi for missing voices");
    rig.run("project.undo");
    CHECK(rig.track(style).instrument->state.empty());
}

TEST_CASE("a Style track with an Arranger plays the project's chords to it, and one without does not") {
    ArrangerRig rig;
    rig.run("arranger.setup");
    const std::string chords = rig.trackOfKind("yamaha.chord");
    const std::string style = rig.trackOfKind("yamaha.style");
    rig.controller->project().editTrack(model::TrackId{chords}, "chords", [](model::Track& track) {
        track.data = Json::parse(R"([{"at":0,"chord":"C"},{"at":3840,"chord":"G7"}])");
    });
    const auto chordSysex = [&] {
        const engine::Session session = app::toSession(rig.document(), rig.registry, rig.path("Project"));
        const auto found = std::ranges::find(session.tracks, style, &engine::Track::id);
        REQUIRE(found != session.tracks.end());
        return std::ranges::count_if(found->midiClips.at(0).events, [](const engine::MidiEvent& event) {
            return event.bytes.size() == 9 && event.bytes[3] == 0x02;
        });
    };
    CHECK(chordSysex() == 2);
    rig.run("instrument.remove", {{"track", style}});
    CHECK(chordSysex() == 0);
}

TEST_CASE("the part tracks play what the Arranger sends them without being armed") {
    ArrangerRig rig;
    rig.run("arranger.setup");
    const std::string style = rig.trackOfKind("yamaha.style");
    rig.run("arranger.style.load", {{"track", style}, {"path", rig.demo.string()}});
    rig.controller->project().editTrack(
        model::TrackId{rig.trackOfKind("yamaha.chord")}, "chords",
        [](model::Track& track) { track.data = Json::parse(R"([{"at":0,"chord":"C"}])"); });
    rig.controller->project().editTrack(model::TrackId{style}, "sections", [](model::Track& track) {
        track.data = Json::parse(R"([{"at":0,"kind":"main","variation":0}])");
    });
    rig.controller->flush();
    CHECK(std::ranges::none_of(rig.document().tracks, &model::Track::armed));

    rig.run("transport.play");
    rig.audio->process(48000);
    rig.run("transport.stop");
    const std::vector<float> output = rig.audio->output("interface", 1);
    CHECK(std::ranges::max(output | std::views::transform([](float sample) { return std::abs(sample); })) > 0.01F);
}

TEST_CASE("export mix and stems render the part tracks the Arranger plays from the Chords and Style lanes") {
    ArrangerRig rig;
    rig.run("arranger.setup");
    const std::string style = rig.trackOfKind("yamaha.style");
    rig.run("arranger.style.load", {{"track", style}, {"path", rig.demo.string()}});
    rig.controller->project().editTrack(
        model::TrackId{rig.trackOfKind("yamaha.chord")}, "chords",
        [](model::Track& track) { track.data = Json::parse(R"([{"at":0,"chord":"C"}])"); });
    rig.controller->project().editTrack(model::TrackId{style}, "sections", [](model::Track& track) {
        track.data = Json::parse(R"([{"at":0,"kind":"main","variation":0}])");
    });

    rig.run("export.mix", {{"path", rig.path("mix.wav").string()}});
    CHECK(loudest(rig.path("mix.wav")) > 0.01F);
    const Json files = rig.run("export.stems", {{"folder", rig.path("stems").string()}}).at("files");
    CHECK(files.size() == kPartNames.size());
    CHECK(loudest(rig.path("stems") / "Bass.wav") > 0.01F);
}

TEST_CASE("an export runs to the end of the style's Ending when the Style lane ends on one") {
    ArrangerRig rig;
    rig.run("arranger.setup");
    const std::string style = rig.trackOfKind("yamaha.style");
    rig.run("arranger.style.load", {{"track", style}, {"path", rig.demo.string()}});
    rig.controller->project().editTrack(model::TrackId{style}, "sections", [](model::Track& track) {
        track.data = Json::parse(R"([{"at":0,"kind":"main","variation":0},{"at":7680,"kind":"ending","variation":0}])");
    });

    const Json result = rig.run("export.stems", {{"folder", rig.path("stems").string()}});
    CHECK(result.at("end").at("bbt") == "4.1.0");
    CHECK(rig.run("project.summary", {}).at("lengthBars") == 3);
}

TEST_CASE("a part waiting for Expansion Voices to read its store warns until the store is read") {
    ArrangerRig rig;
    rig.run("arranger.setup");
    const std::string style = rig.trackOfKind("yamaha.style");
    rig.YamahaRig::run("arranger.style.load", {{"track", style}, {"path", rig.demo.string()}});
    const auto waitWarnings = [&] {
        return std::ranges::count_if(
            yamaha::partWarnings(rig.track(style), rig.document(), rig.controller->instruments()),
            [](const engine::SessionError& warning) { return warning.message.starts_with("Wait for Expansion"); });
    };
    CHECK((waitWarnings() > 0) == rig.voicesLoading());

    REQUIRE(processUntil([&] { return !rig.voicesLoading(); }, 10000));
    CHECK(waitWarnings() == 0);
}

TEST_CASE("a style whose Mains leave Rhythm 1 empty plays no drums, as an Audio Style") {
    arranger::Style style = arranger::parseStyle(arranger::test::demoStyle());
    CHECK(yamaha::stylePlaysDrums(style));
    for (arranger::StyleSection& section : style.sections) {
        std::erase_if(section.events, [](const arranger::StyleEvent& event) {
            return (event.bytes[0] & 0x0FU) == arranger::test::kDrums;
        });
    }
    CHECK_FALSE(yamaha::stylePlaysDrums(style));
}
