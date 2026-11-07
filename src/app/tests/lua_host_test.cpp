#include "emerick/app/lua_host.hpp"

#include <QTemporaryDir>
#include <QTimer>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "emerick/model/automation.hpp"
#include "project_rig.hpp"
#include "qt_app.hpp"

using namespace emerick;
using namespace emerick::app;

namespace {

struct Fixture {
    QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    UiApi ui{registry};
    LuaHost lua{registry, ui};

    Fixture() {
        registry.addAction({.id = "test.echo", .title = "Echo", .run = [](const Json& params) { return params; }});
        registry.addSetting({.id = "audio.bufferSize",
                             .title = "Audio buffer size",
                             .type = SettingType::Integer,
                             .defaultValue = 256});
        registry.addStateProvider("project", [] { return Json{{"tracks", {{{"id", "a1"}, {"mute", false}}}}}; });
    }
};

}  // namespace

TEST_CASE("the sandbox removes file, process and module access") {
    Fixture fixture;
    const Json result = fixture.lua.eval(
        "return {io == nil, package == nil, debug == nil, dofile == nil, loadfile == nil, require == nil, "
        "unpack == nil, os.execute == nil, os.remove == nil, os.rename == nil, os.tmpname == nil, os.getenv == nil, "
        "os.exit == nil, os.setlocale == nil}");
    CHECK(result.at("ok") == true);
    CHECK(result.at("result") == Json(std::vector(14, true)));
    CHECK(fixture.lua.eval("return string.upper('ok')").at("result") == "OK");
    CHECK(fixture.lua.eval("return ('E7(#9)'):match('^([A-G])([#b]?)')").at("result") == "E");
    CHECK(fixture.lua.eval("return (('a-b'):gsub('-', '+'))").at("result") == "a+b");
}

TEST_CASE("a backtracking pattern fails fast and plain patterns still match") {
    Fixture fixture;
    for (const char* call : {"s:find(p)", "s:match(p)", "s:gmatch(p)()", "s:gsub(p, '')"}) {
        const Json result = fixture.lua.eval(
            std::string{"local s, p = string.rep('a', 22), string.rep('a*', 22) .. 'b' return "} + call);
        CHECK(result.at("ok") == false);
        CHECK(result.at("error").get<std::string>().find("pattern too complex") != std::string::npos);
    }
    CHECK(fixture.lua.eval("local n = 0 for _ in string.rep('ab ', 1e6):gmatch('%a+') do n = n + 1 end return n")
              .at("result") == 1'000'000);
    CHECK(fixture.lua.eval("return (string.rep('a', 1e6):gsub('a', 'b')):sub(-3)").at("result") == "bbb");
}

TEST_CASE("a script reads the clock and formats dates with os") {
    Fixture fixture;
    const Json result = fixture.lua.eval(
        "return {math.type(os.time()), os.clock() >= 0, os.date('!%Y', 0), os.difftime(os.time(), os.time() - 5)}");
    CHECK(result.at("result") == Json::array({"integer", true, "1970", 5.0}));
}

TEST_CASE("load runs a text chunk and refuses bytecode") {
    Fixture fixture;
    CHECK(fixture.lua.eval("x = 2 return load('return x * 21')()").at("result") == 42);
    CHECK(fixture.lua.eval("return load('return y', 'chunk', 't', {y = 7})()").at("result") == 7);
    const Json bytecode = fixture.lua.eval("return select(2, load(string.dump(function() return 1 end), 'b', 'b'))");
    CHECK(bytecode.at("result").get<std::string>().find("attempt to load a binary chunk") != std::string::npos);
}

TEST_CASE("a coroutine yields and resumes") {
    Fixture fixture;
    const Json result = fixture.lua.eval(
        "local co = coroutine.create(function(a) local b = coroutine.yield(a + 1) return b * 2 end) "
        "local _, first = coroutine.resume(co, 1) local _, second = coroutine.resume(co, 10) "
        "local gen = coroutine.wrap(function() for i = 1, 3 do coroutine.yield(i) end end) "
        "return {first, second, gen() + gen() + gen(), coroutine.status(co)}");
    CHECK(result.at("result") == Json::array({2, 20, 6, "dead"}));
}

TEST_CASE("a runaway coroutine stops the whole script") {
    Fixture fixture;
    for (const char* script : {"coroutine.resume(coroutine.create(function() while true do end end)) while true do end",
                               "while true do coroutine.resume(coroutine.create(function() while true do end end)) end",
                               "while true do pcall(coroutine.wrap(function() while true do end end)) end"}) {
        const Json result = fixture.lua.eval(script);
        CHECK(result.at("ok") == false);
        CHECK(result.at("error").get<std::string>().find("instructions") != std::string::npos);
    }
    fixture.registry.setLimits({.luaTimeout = std::chrono::milliseconds{50}});
    const Json timedOut =
        fixture.lua.eval("while true do coroutine.resume(coroutine.create(function() while true do end end)) end");
    CHECK(timedOut.at("error").get<std::string>().find("script ran longer than 50 ms") != std::string::npos);
    CHECK(fixture.lua.eval("return coroutine.wrap(function() return 1 end)()").at("result") == 1);
}

TEST_CASE("a script cannot take more memory than its budget") {
    Fixture fixture;
    const Json result = fixture.lua.eval("return #string.rep('x', 1 << 30)");
    CHECK(result.at("ok") == false);
    CHECK(fixture.lua.eval("return #string.rep('x', 1000)").at("result") == 1000);
}

TEST_CASE("a result whose metamethods fail still converts") {
    Fixture fixture;
    const Json result = fixture.lua.eval("return setmetatable({}, {__len = function() error('x') end})");
    CHECK(result.at("ok") == true);
    CHECK(result.at("result") == Json::object());
    const Json indexed = fixture.lua.eval(
        "return setmetatable({1, 2}, {__index = function() error('x') end, __pairs = function() error('x') end})");
    CHECK(indexed.at("result") == Json::array({1, 2}));
    CHECK(fixture.lua.eval("print(setmetatable({}, {__tostring = function() error('x') end}))").at("ok") == false);
    CHECK(fixture.lua.eval("return 1").at("result") == 1);
}

TEST_CASE("print is captured and errors come back as data") {
    Fixture fixture;
    const Json printed = fixture.lua.eval("print('hello', 42) print(nil)");
    CHECK(printed.at("output") == "hello\t42\nnil\n");
    CHECK(printed.at("result").is_null());

    const Json failed = fixture.lua.eval("print('before') error('boom')");
    CHECK(failed.at("ok") == false);
    CHECK(failed.at("error").get<std::string>().find("boom") != std::string::npos);
    CHECK(failed.at("output") == "before\n");
    CHECK(fixture.lua.eval("return (").at("ok") == false);
}

TEST_CASE("JSON and Lua tables convert both ways") {
    Fixture fixture;
    const Json params{{"n", 3}, {"x", 1.5}, {"list", {1, "two", true}}, {"nested", {{"ok", true}}}};
    const Json result =
        fixture.lua.eval("return emerick.run('test.echo', " +
                         std::string{"{n = 3, x = 1.5, list = {1, 'two', true}, nested = {ok = true}})"});
    CHECK(result.at("result") == params);
    CHECK(fixture.lua.eval("local t = {} t.self = t return t").at("ok") == false);
}

TEST_CASE("Lua text that is not UTF-8 reaches actions with the bad bytes replaced") {
    Fixture fixture;
    const Json result = fixture.lua.eval(R"(return emerick.run('test.echo', {name = "a\255b", ["k\254"] = 1}))");
    CHECK(result.at("result") == Json{{"name", "a�b"}, {"k�", 1}});
}

TEST_CASE("emerick functions reach the registry") {
    Fixture fixture;
    CHECK(fixture.lua.eval("return emerick.setting('audio.bufferSize')").at("result") == 256);
    CHECK(fixture.lua.eval("emerick.setting('audio.bufferSize', 512) return emerick.setting('audio.bufferSize')")
              .at("result") == 512);
    CHECK(fixture.registry.settingValue("audio.bufferSize") == 512);
    CHECK(fixture.lua.eval("return emerick.state('project').tracks[1].id").at("result") == "a1");
    CHECK(fixture.lua.eval("return emerick.actions('echo')[1].id").at("result") == "test.echo");
    CHECK(fixture.lua.eval("return emerick.run('test.missing')").at("ok") == false);
}

TEST_CASE("a runaway script stops") {
    Fixture fixture;
    const Json result = fixture.lua.eval("while true do end");
    CHECK(result.at("ok") == false);
    CHECK(fixture.lua.eval("return 1").at("result") == 1);
}

TEST_CASE("pcall cannot catch the runaway stop") {
    Fixture fixture;
    const Json result = fixture.lua.eval("while true do pcall(function() while true do end end) end");
    CHECK(result.at("ok") == false);
    CHECK(result.at("error").get<std::string>().find("instructions") != std::string::npos);
    CHECK(fixture.lua.eval("return pcall(error, 'caught')").at("result") == false);
}

TEST_CASE("the Lua time limit stops a runaway script and leaves short scripts alone") {
    Fixture fixture;
    fixture.registry.setLimits({.luaTimeout = std::chrono::milliseconds{50}});
    const Json result = fixture.lua.eval("while true do pcall(function() while true do end end) end");
    CHECK(result.at("ok") == false);
    CHECK(result.at("error").get<std::string>().find("script ran longer than 50 ms") != std::string::npos);
    CHECK(fixture.lua.eval("local sum = 0 for i = 1, 1000 do sum = sum + i end return sum").at("result") == 500500);
}

TEST_CASE("the Lua memory limit fails a huge string and leaves small ones alone") {
    Fixture fixture;
    fixture.registry.setLimits({.luaMemoryBytes = std::size_t{16} << 20});
    CHECK(fixture.lua.eval("return #string.rep('x', 1e10)").at("ok") == false);
    const Json over = fixture.lua.eval("return #string.rep('x', 32 << 20)");
    CHECK(over.at("ok") == false);
    CHECK(over.at("error").get<std::string>().find("not enough memory") != std::string::npos);
    CHECK(fixture.lua.eval("return #string.rep('x', 1 << 20)").at("result") == 1 << 20);
}

TEST_CASE("ui.set sets a setting like the MCP tool does") {
    Fixture fixture;
    const Json result = fixture.lua.eval("return emerick.ui.set('audio.bufferSize', 1024).setting.value");
    CHECK(result.at("result") == 1024);
    CHECK(fixture.lua.eval("return emerick.ui.set('nothing', 1)").at("ok") == false);
}

namespace {

struct ScriptRun {
    int code = 0;
    std::string out;
    std::string err;
};

ScriptRun runScript(Fixture& fixture, const std::string& code, const std::vector<std::string>& args = {}) {
    const std::filesystem::path script = fixture.dir.filePath("build.lua").toStdString();
    std::ofstream(script) << code;
    std::ostringstream out;
    std::ostringstream err;
    const int exitCode = runCommandLineScript(fixture.lua, script, args, out, err);
    return {.code = exitCode, .out = out.str(), .err = err.str()};
}

}  // namespace

TEST_CASE("a command line script reads its arguments and reports its error with the line") {
    Fixture fixture;
    const ScriptRun ok = runScript(fixture, "print(arg[0]:find('build.lua') ~= nil, arg[1], arg[2], #arg)", {"a", "b"});
    CHECK(ok.code == 0);
    CHECK(ok.out == "true\ta\tb\t2\n");
    CHECK(ok.err.empty());

    const ScriptRun failed = runScript(fixture, "print('before')\nerror('boom')");
    CHECK(failed.code == 1);
    CHECK(failed.out == "before\n");
    CHECK(failed.err == "build.lua:2: boom\n");

    std::ostringstream out;
    std::ostringstream err;
    CHECK(runCommandLineScript(fixture.lua, fixture.dir.filePath("missing.lua").toStdString(), {}, out, err) == 2);
    CHECK(err.str().starts_with("cannot read script"));
}

TEST_CASE("only a command line script can wait, and it lets queued work run") {
    ensureApplication();
    Fixture fixture;
    const Json refused = fixture.lua.eval("emerick.wait(0)");
    CHECK(refused.at("ok") == false);
    CHECK(refused.at("error").get<std::string>().find("--script") != std::string::npos);

    bool ready = false;
    fixture.registry.addAction(
        {.id = "test.ready", .title = "Ready", .run = [&ready](const Json&) { return Json(ready); }});
    QTimer::singleShot(20, [&ready] { ready = true; });
    const ScriptRun waited = runScript(fixture,
                                       "print(emerick.wait(5, function() return emerick.run('test.ready') end))\n"
                                       "print(emerick.wait(0.01))");
    CHECK(waited.code == 0);
    CHECK(waited.out == "true\nfalse\n");

    const ScriptRun nested = runScript(fixture, "emerick.wait(1, function() return emerick.wait(0) end)");
    CHECK(nested.code == 1);
    CHECK(nested.err.find("--script") != std::string::npos);
    const ScriptRun recovered = runScript(fixture,
                                          "print(pcall(emerick.wait, -1))\n"
                                          "print(pcall(emerick.wait, 0 / 0))\n"
                                          "print(pcall(emerick.wait, 1, function() error('x') end))\n"
                                          "print(emerick.wait(0))");
    CHECK(recovered.code == 0);
    CHECK(
        recovered.out.starts_with("false\temerick.wait takes from 0 to 86400 seconds\n"
                                  "false\temerick.wait takes from 0 to 86400 seconds\n"
                                  "false\tbuild.lua:3: x\n"));
    CHECK(recovered.out.ends_with("\nfalse\n"));
    CHECK(fixture.lua.eval("emerick.wait(0)").at("ok") == false);
}

TEST_CASE("a command line script's wait counts toward no time limit") {
    ensureApplication();
    Fixture fixture;
    fixture.registry.setLimits({.luaTimeout = std::chrono::milliseconds{100}});
    const ScriptRun waited = runScript(fixture, "print(emerick.wait(0.3))\nprint(#string.rep('x', 10))");
    CHECK(waited.code == 0);
    CHECK(waited.out == "false\n10\n");
    const ScriptRun runaway = runScript(fixture, "emerick.wait(0.01)\nwhile true do end");
    CHECK(runaway.code == 1);
    CHECK(runaway.err.find("script ran longer than 100 ms") != std::string::npos);
}

namespace {

struct ProjectLua {
    test::ProjectRig rig;
    UiApi ui{rig.registry};
    LuaHost lua{rig.registry, ui};

    Json eval(const std::string& code) {
        const Json result = lua.eval(code);
        INFO(result.dump());
        REQUIRE(result.at("ok") == true);
        return result.at("result");
    }

    [[nodiscard]] const model::Track& track(const std::string& id) const {
        return *rig.controller->project().track(model::TrackId{id});
    }
};

}  // namespace

TEST_CASE("emerick.bus adds a bus with its inserts in order and their params set") {
    ProjectLua project;
    const Json ids = project.eval(
        "local bus, inserts = emerick.bus('Verb', {{compressor = {ratio = 2}}, {reverb = {mix = 100, size = 80}}})\n"
        "return {bus, inserts}");
    const model::Track& bus = project.track(ids.at(0));
    CHECK(bus.name == "Verb");
    CHECK(bus.kind == "bus");
    REQUIRE(bus.inserts.size() == 2);
    CHECK(ids.at(1) == Json{bus.inserts[0].id.value, bus.inserts[1].id.value});
    CHECK(bus.inserts[0].type == "compressor");
    CHECK(bus.inserts[0].params.at("ratio") == 2.0);
    CHECK(bus.inserts[1].type == "reverb");
    CHECK(bus.inserts[1].params.at("mix") == 100.0);
    CHECK(bus.inserts[1].params.at("size") == 80.0);

    CHECK(project.track(project.eval("return emerick.bus('Plain')")).inserts.empty());
    CHECK(project.track(project.eval("return emerick.bus('Echo', {delay = {}})")).inserts.size() == 1);
    CHECK(project.lua.eval("emerick.bus('Bad', {reverb = {}, delay = {}})").at("ok") == false);
}

TEST_CASE("emerick.bus with a bad effect, param or value leaves the project unchanged") {
    ProjectLua project;
    const model::Document before = project.rig.controller->project().document();
    CHECK(project.lua.eval("emerick.bus('Bad', {{reverb = {mix = 50}}, {phaser = {}}})").at("ok") == false);
    CHECK(project.lua.eval("emerick.bus('Bad', {{reverb = {mix = 50}}, {delay = {wobble = 1}}})").at("ok") == false);
    CHECK(project.lua.eval("emerick.bus('Bad', {{reverb = {mix = 50}}, {delay = {note = '1/5'}}})").at("ok") == false);
    CHECK(project.rig.controller->project().document() == before);
}

TEST_CASE("emerick.automate places points by bar and beat on the project's meter") {
    ProjectLua project;
    const std::string vox = project.rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    project.rig.run("project.setTempo", {{"bpm", 90}, {"numerator", 3}, {"denominator", 4}});
    const Json points = project.eval("return emerick.automate('Vox', 'gain', {{1, 1, -6}, {2, 2.5, 0, 'smooth'}})");
    CHECK(points.size() == 2);
    const model::AutomationLane* lane = model::findLane(project.track(vox), "gain");
    REQUIRE(lane != nullptr);
    REQUIRE(lane->points.size() == 2);
    CHECK(lane->points[0].tick == 0);
    CHECK(lane->points[0].value == -6.0);
    CHECK(lane->points[1].tick == (3 * 960) + 960 + 480);
    CHECK(lane->points[1].curve == core::AutomationCurve::smooth);
    CHECK(project.lua.eval("emerick.automate('Vox', 'gain', {{0, 1, 0}})").at("ok") == false);
    CHECK(project.lua.eval("emerick.automate('Vox', 'gain', {{1, 1}})").at("ok") == false);
}

TEST_CASE("emerick.automate also takes points as named tables, by bar and beat or by position") {
    ProjectLua project;
    const std::string vox = project.rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    const Json points = project.eval(
        "return emerick.automate('Vox', 'gain', {{bar = 2, value = -3}, {bar = 3, beat = 2.5, value = -1, curve = "
        "'hold'}, {at = '4.1.0', value = 0}, {at = 480, value = -6}})");
    CHECK(points.size() == 4);
    const model::AutomationLane* lane = model::findLane(project.track(vox), "gain");
    REQUIRE(lane != nullptr);
    REQUIRE(lane->points.size() == 4);
    CHECK(lane->points[0].tick == 480);
    CHECK(lane->points[0].value == -6.0);
    CHECK(lane->points[1].tick == 3840);
    CHECK(lane->points[2].tick == (2 * 3840) + 960 + 480);
    CHECK(lane->points[2].curve == core::AutomationCurve::hold);
    CHECK(lane->points[3].tick == 3 * 3840);
    for (const char* bad : {"{value = 0}", "{bar = 1}", "{at = 0, value = 'loud'}", "{bar = 'one', value = 0}"}) {
        INFO(bad);
        CHECK(project.lua.eval(std::string{"emerick.automate('Vox', 'gain', {"} + bad + "})").at("ok") == false);
    }
}

TEST_CASE("emerick.automate with a bad bar, beat, value or curve leaves the project unchanged") {
    ProjectLua project;
    project.rig.run("track.addAudio", {{"name", "Vox"}});
    const model::Document before = project.rig.controller->project().document();
    for (const char* bad : {"{0, 1, 0}", "{1.5, 1, 0}", "{1, 0.5, 0}", "{1, 1, 100}", "{1, 1, 0, 'wavy'}"}) {
        INFO(bad);
        CHECK(project.lua.eval(std::string{"emerick.automate('Vox', 'gain', {{1, 1, -6}, "} + bad + "})").at("ok") ==
              false);
    }
    CHECK(project.rig.controller->project().document() == before);
}

TEST_CASE("emerick.waitReady waits for loading instruments and raises after its timeout") {
    ProjectLua project;
    bool loading = true;
    project.rig.registry.addLoadingTracks([&](const model::Document& document) {
        return loading ? std::vector{document.tracks.front().id} : std::vector<model::TrackId>{};
    });
    project.rig.run("track.addMidi", {{"name", "Keys"}});
    const Json failed = project.lua.eval("emerick.waitReady(0.05)");
    CHECK(failed.at("ok") == false);
    CHECK(failed.at("error").get<std::string>().find("Keys still loading") != std::string::npos);
    QTimer::singleShot(20, [&loading] { loading = false; });
    CHECK(project.lua.eval("emerick.waitReady(5)").at("ok") == true);
}

TEST_CASE("a script reads the windows of track.analyze and the issues of mix.inspect") {
    ProjectLua project;
    test::writeConstant(project.rig.path("offset.wav"), 4 * static_cast<core::Frames>(test::kRate), 0.2F);
    const Json result = project.eval(
        "local id = emerick.run('track.addAudio', {name = 'Offset'}).id\n"
        "emerick.run('clip.import', {track = id, path = '" +
        project.rig.path("offset.wav").string() +
        "'})\n"
        "local windows = emerick.run('track.analyze', {track = id, resolution = 'bar'}).windows\n"
        "local issues = emerick.run('mix.inspect').issues\n"
        "return {#windows, windows[2].at.bbt, issues[1].kind}");
    CHECK(result == Json{2, "2.1.0", "dc"});
}
