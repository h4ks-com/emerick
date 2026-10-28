#include <QByteArray>
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "emerick/app/session_mapping.hpp"
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

struct ArrangerRig : YamahaRig {
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

// The demo style with its bass on another program, which no store in these tests holds.
std::filesystem::path otherBassStyle(const ArrangerRig& rig) {
    Bytes bytes = arranger::test::demoStyle();
    const std::array<std::uint8_t, 2> bassProgram{0xC0 | arranger::test::kBass, 33};
    const auto found = std::ranges::search(bytes, bassProgram);
    REQUIRE_FALSE(found.empty());
    found[1] = 34;
    const std::filesystem::path path = rig.path("Other.sty");
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

}  // namespace

TEST_CASE("choosing a style switches the part tracks it has voices for between Expansion Voices and General MIDI") {
    ArrangerRig rig;
    const HomeFolder home{rig.path("home")};
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

    rig.run("project.undo");
    CHECK(rig.track(style).instrument->state.empty());
    CHECK(plugin(2) == "org.emerick.gm");

    rig.run("project.redo");
    CHECK(plugin(2) == bassPlugin);
    rig.run("arranger.style.load", {{"track", style}, {"path", otherBassStyle(rig).string()}});
    CHECK(plugin(2) == "org.emerick.gm");
    CHECK(plugin(3).empty());
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

TEST_CASE("a style without drum notes in its Mains plays no drums, as an Audio Style") {
    arranger::Style style = arranger::parseStyle(arranger::test::demoStyle());
    CHECK(yamaha::stylePlaysDrums(style));
    for (arranger::StyleSection& section : style.sections) {
        std::erase_if(section.events, [](const arranger::StyleEvent& event) {
            return (event.bytes[0] & 0x0FU) == arranger::test::kDrums;
        });
    }
    CHECK_FALSE(yamaha::stylePlaysDrums(style));
}
