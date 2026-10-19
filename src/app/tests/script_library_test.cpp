#include "emerick/app/script_library.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fstream>

#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

void writeScript(const std::filesystem::path& path, const std::string& source) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << source;
}

struct ScriptRig {
    ProjectRig rig;
    UiApi ui{rig.registry};
    LuaHost lua{rig.registry, ui};
    Console console{rig.registry, lua};
    std::filesystem::path userFolder = rig.path("user scripts");

    ScriptRig() { registerConsoleActions(rig.registry, rig.nav, console); }
};

}  // namespace

TEST_CASE("a script header names the title, shortcut and description and stops at the first code line") {
    const ScriptHeader header = parseScriptHeader(
        "\n-- title: Mute all\n--shortcut:Ctrl+Alt+M\n-- description: Mute every track.\n-- note: kept out\n"
        "print('x')\n-- title: ignored\n");
    CHECK(header == ScriptHeader{"Mute all", "Ctrl+Alt+M", "Mute every track."});
    CHECK(parseScriptHeader("return 1") == ScriptHeader{});
}

TEST_CASE("a script registers as an action in the Scripts menu with its title and shortcut") {
    ScriptRig fixture;
    writeScript(fixture.userFolder / "mute.lua",
                "-- title: Mute all\n-- shortcut: Ctrl+Alt+M\n-- description: Mute every track.\nprint('muted')\n");
    writeScript(fixture.userFolder / "taken.lua", "-- shortcut: Ctrl+K\nreturn 1\n");
    writeScript(fixture.userFolder / "notes.txt", "not a script");
    fixture.rig.registry.addAction(
        {.id = "test.search", .shortcut = "Ctrl+K", .run = [](const Json&) { return Json{}; }});
    ScriptLibrary library(fixture.rig.registry, fixture.console, fixture.userFolder);
    registerScriptActions(fixture.rig.registry, library);

    const Action* script = fixture.rig.registry.action("scripts.user.mute");
    REQUIRE(script != nullptr);
    CHECK(script->title == "Mute all");
    CHECK(script->shortcut == "Ctrl+Alt+M");
    CHECK(script->description == "Mute every track.");
    CHECK(script->menus == std::vector<std::string>{kScriptsMenu});
    CHECK(fixture.rig.registry.action("scripts.user.taken")->shortcut.empty());
    CHECK(fixture.rig.registry.action("scripts.user.taken")->title == "taken");
    CHECK(library.scripts().size() == 2);

    fixture.rig.run("scripts.user.mute");
    CHECK(std::ranges::contains(fixture.console.entries(), std::string{"muted"}, &ConsoleEntry::text));
    CHECK(fixture.console.entries().back().text == R"(emerick.run("scripts.user.mute"))");
    CHECK(fixture.rig.run("script.run", {{"name", "Mute all"}}).at("output") == "muted\n");
    CHECK(fixture.rig.run("script.list").at("scripts").size() == 2);
    CHECK_THROWS_AS(fixture.rig.run("script.run", {{"name", "missing"}}), AppError);

    fixture.rig.registry.setShortcut("scripts.user.mute", "Ctrl+Alt+U");
    writeScript(fixture.userFolder / "mute.lua", "-- title: Mute every track\n-- shortcut: Ctrl+Alt+M\n");
    library.rescan();
    CHECK(fixture.rig.registry.action("scripts.user.mute")->title == "Mute every track");
    CHECK(fixture.rig.registry.action("scripts.user.mute")->shortcut == "Ctrl+Alt+U");
}

TEST_CASE("the folder watcher picks up a new script and drops a deleted one") {
    ScriptRig fixture;
    std::filesystem::create_directories(fixture.userFolder);
    ScriptLibrary library(fixture.rig.registry, fixture.console, fixture.userFolder);
    CHECK(library.scripts().empty());

    writeScript(fixture.userFolder / "hello.lua", "-- title: Say hello\nprint('hello')\n");
    CHECK(processUntil([&] { return fixture.rig.registry.action("scripts.user.hello") != nullptr; }, 5000));
    CHECK(fixture.rig.registry.action("scripts.user.hello")->title == "Say hello");

    std::filesystem::remove(fixture.userFolder / "hello.lua");
    CHECK(processUntil([&] { return fixture.rig.registry.action("scripts.user.hello") == nullptr; }, 5000));
}

TEST_CASE("project scripts load from the bundle and a failing script reports its line") {
    ScriptRig fixture;
    const std::filesystem::path projectScripts = fixture.rig.controller->folder() / "scripts";
    writeScript(projectScripts / "broken.lua", "local x = 1\nerror('broken')\n");
    ScriptLibrary library(fixture.rig.registry, fixture.console, fixture.userFolder);
    library.setProjectFolder(projectScripts);
    REQUIRE(fixture.rig.registry.action("scripts.project.broken") != nullptr);
    CHECK_THROWS_AS(fixture.rig.run("scripts.project.broken"), AppError);
    CHECK(fixture.console.entries().back().text == "broken.lua:2: broken");
}

TEST_CASE("the example scripts copy into an empty user folder and run") {
    ScriptRig fixture;
    copyExampleScripts(fixture.userFolder);
    ScriptLibrary library(fixture.rig.registry, fixture.console, fixture.userFolder);
    CHECK(library.scripts().size() == 3);
    CHECK(fixture.rig.registry.action("scripts.user.mute-all")->title == "Mute all tracks");

    const auto audio = fixture.rig.run("track.addAudio").at("id").get<std::string>();
    const auto midi = fixture.rig.run("track.addMidi").at("id").get<std::string>();
    fixture.rig.run("scripts.user.mute-all");
    fixture.rig.run("scripts.user.arm-midi");
    fixture.rig.run("scripts.user.rename-from-list");
    const model::Track* first = fixture.rig.controller->project().track(model::TrackId{audio});
    const model::Track* second = fixture.rig.controller->project().track(model::TrackId{midi});
    CHECK((first->mute && second->mute));
    CHECK((!first->armed && second->armed));
    CHECK((first->name == "Keys" && second->name == "Bass"));

    std::ofstream(fixture.userFolder / "mine.lua") << "return 1\n";
    std::filesystem::remove(fixture.userFolder / "mute-all.lua");
    copyExampleScripts(fixture.userFolder);
    CHECK(!std::filesystem::exists(fixture.userFolder / "mute-all.lua"));
}
