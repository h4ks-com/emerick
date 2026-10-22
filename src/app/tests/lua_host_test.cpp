#include "emerick/app/lua_host.hpp"

#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>

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
        "return {io == nil, os == nil, package == nil, debug == nil, dofile == nil, loadfile == nil, require == nil, "
        "coroutine == nil, string.gsub == nil}");
    CHECK(result.at("ok") == true);
    CHECK(result.at("result") == Json::array({true, true, true, true, true, true, true, true, true}));
    CHECK(fixture.lua.eval("return string.upper('ok')").at("result") == "OK");
    CHECK(fixture.lua.eval("return string.find('a.b', '.', 1)").at("result") == 2);
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

TEST_CASE("ui.set sets a setting like the MCP tool does") {
    Fixture fixture;
    const Json result = fixture.lua.eval("return emerick.ui.set('audio.bufferSize', 1024).setting.value");
    CHECK(result.at("result") == 1024);
    CHECK(fixture.lua.eval("return emerick.ui.set('nothing', 1)").at("ok") == false);
}
