#include "emerick/app/registry.hpp"

#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>

#include "emerick/app/core_actions.hpp"
#include "emerick/app/nav.hpp"

using namespace emerick::app;

namespace {

Action echo(std::string id, std::string title) {
    return {.id = std::move(id), .title = std::move(title), .run = [](const Json& params) { return params; }};
}

}  // namespace

TEST_CASE("actions run with params and refuse when disabled or unknown") {
    QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    bool enabled = true;
    Action action = echo("test.echo", "Echo");
    action.enabled = [&enabled] { return enabled; };
    registry.addAction(action);

    CHECK(registry.run("test.echo", {{"x", 1}}) == Json{{"x", 1}});
    enabled = false;
    CHECK_THROWS_AS(registry.run("test.echo"), AppError);
    CHECK_THROWS_AS(registry.run("test.missing"), AppError);
    CHECK_THROWS_AS(registry.addAction(echo("test.echo", "Again")), std::logic_error);
}

TEST_CASE("settings validate, persist and reject read only writes") {
    QTemporaryDir dir;
    const QString path = dir.filePath("settings.ini");
    const Setting buffer{.id = "audio.bufferSize",
                         .title = "Audio buffer size",
                         .type = SettingType::Choice,
                         .defaultValue = 256,
                         .choices = {128, 256, 512}};
    const Setting accent{.id = "appearance.accent",
                         .title = "Accent color",
                         .type = SettingType::Color,
                         .defaultValue = "#F2B544",
                         .readOnly = true};
    const Setting port{.id = "mcp.port",
                       .title = "MCP port",
                       .type = SettingType::Integer,
                       .defaultValue = 7777,
                       .minimum = 0,
                       .maximum = 65535};
    {
        Registry registry{path};
        registry.addSetting(buffer);
        registry.addSetting(accent);
        registry.addSetting(port);
        CHECK_THROWS_AS(registry.setSetting("mcp.port", 65536), AppError);
        CHECK_THROWS_AS(registry.setSetting("mcp.port", -1), AppError);
        CHECK_THROWS_AS(registry.setSetting("mcp.port", 1.5), AppError);
        CHECK(registry.setSetting("mcp.port", 0) == 0);
        CHECK(registry.describeSetting("mcp.port").at("maximum") == 65535);
        CHECK(registry.settingValue("audio.bufferSize") == 256);
        CHECK(registry.setSetting("audio.bufferSize", 512) == 512);
        CHECK_THROWS_AS(registry.setSetting("audio.bufferSize", "big"), AppError);
        CHECK_THROWS_AS(registry.setSetting("audio.bufferSize", 300), AppError);
        CHECK_THROWS_AS(registry.setSetting("appearance.accent", "#000000"), AppError);
        CHECK(registry.settingsMap().value("audio.bufferSize").toInt() == 512);
    }
    Registry reopened{path};
    reopened.addSetting(buffer);
    CHECK(reopened.settingValue("audio.bufferSize") == 512);
}

TEST_CASE("search ranks titles, matches aliases and reports UTF-16 ranges") {
    QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    Action preroll = echo("transport.preroll", "Pre-roll");
    preroll.keywords = {"recording"};
    registry.addAction(preroll);
    registry.addAction(echo("transport.record", "Record"));
    registry.addSetting(
        {.id = "audio.bufferSize", .title = "Audio buffer size", .aliases = {"latency"}, .type = SettingType::Integer});

    const auto hits = registry.search("rec");
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].id == "transport.record");
    CHECK(hits[1].id == "transport.preroll");
    CHECK(registry.search("latency").front().kind == EntryKind::Setting);

    const QVariantMap first = registry.palette("rec").front().toMap();
    CHECK(first.value("id").toString() == "transport.record");
    CHECK(first.value("ranges").toList().front().toList() == QVariantList{0, 3});
}

TEST_CASE("reveal walks locations from the root") {
    QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    std::vector<std::string> steps;
    registry.addLocation({.id = "settings", .reveal = [&steps] { steps.emplace_back("settings"); }});
    registry.addLocation(
        {.id = "settings.audio", .parent = "settings", .reveal = [&steps] { steps.emplace_back("settings.audio"); }});
    registry.addSetting({.id = "audio.bufferSize",
                         .title = "Audio buffer size",
                         .type = SettingType::Integer,
                         .location = "settings.audio"});
    registry.addAction(echo("test.echo", "Echo"));

    const Revealed revealed = registry.reveal("audio.bufferSize");
    CHECK(revealed.target == "audio.bufferSize");
    CHECK(revealed.chain == std::vector<std::string>{"settings", "settings.audio"});
    CHECK(steps == revealed.chain);
    CHECK_THROWS_AS(registry.reveal("test.echo"), AppError);
}

TEST_CASE("state providers, track types and MIDI device profiles register once") {
    QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    registry.addStateProvider("project", [] { return Json{{"tracks", Json::array()}}; });
    registry.addTrackType({.kind = "chord", .title = "Chord", .icon = "chord", .laneComponent = "qrc:/Chord.qml"});
    registry.addMidiDeviceProfile({.nameMatch = "Digital Keyboard", .channelNames = {{1, "Right 1"}}});

    CHECK(registry.state("project") == Json{{"tracks", Json::array()}});
    CHECK(registry.allState().contains("project"));
    CHECK_THROWS_AS(registry.state("engine"), AppError);
    CHECK(registry.trackTypes().front().kind == "chord");
    REQUIRE(registry.midiDeviceProfile("Digital Keyboard Port 1") != nullptr);
    CHECK(registry.midiDeviceProfile("Digital Keyboard Port 1")->channelNames.at(1) == "Right 1");
    CHECK(registry.midiDeviceProfile("IAC Driver") == nullptr);
}

TEST_CASE("core actions drive Nav") {
    QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    Nav nav;
    registerCoreActions(registry, nav, {.audio = "null", .midi = "virtual"});

    CHECK_THROWS_AS(registry.run("settings.close"), AppError);
    registry.run("settings.show.audio");
    CHECK(nav.page() == Page::Settings);
    CHECK(nav.settingsTab() == "audio");
    CHECK(registry.state("nav").at("settingsTab") == "audio");
    registry.run("settings.close");
    CHECK(nav.page() == Page::Main);
    CHECK(registry.run("app.about").at("name") == "Emerick");
    CHECK(nav.aboutOpen());
    CHECK(registry.reveal("mcp.port").chain == std::vector<std::string>{"settings", "settings.general"});
    CHECK(registry.state("app").at("audio") == "null");
}

TEST_CASE("the settings page reads its sections and rows from the registry") {
    QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    Nav nav;
    registerCoreActions(registry, nav, {});

    const QVariantList sections = registry.sections("settings");
    REQUIRE(sections.size() == 2);
    CHECK(sections.at(0).toMap().value("id").toString() == "settings.general");
    CHECK(sections.at(1).toMap().value("title").toString() == "Audio");
    const QVariantList audio = registry.settingsAt("settings.audio");
    REQUIRE(audio.size() == 1);
    CHECK(audio.front().toMap().value("type").toInt() == static_cast<int>(SettingType::Choice));
    CHECK(registry.actionInfo("palette.open").value("shortcut").toString() == "Ctrl+K");
}

TEST_CASE("JSON and QVariant convert both ways") {
    const Json value{{"n", 3}, {"list", {1, "two", true}}, {"none", nullptr}};
    CHECK(toJson(toVariant(value)) == value);
    CHECK(toJson(QVariant{512.0}) == 512);
}
