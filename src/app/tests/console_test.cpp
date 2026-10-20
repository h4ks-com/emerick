#include "emerick/app/console.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QTextBlock>
#include <QTextDocument>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <ranges>

#include "emerick/app/console_completion.hpp"
#include "emerick/app/core_actions.hpp"
#include "emerick/app/mcp_tools.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

struct ConsoleRig {
    ProjectRig rig;
    UiApi ui{rig.registry};
    LuaHost lua{rig.registry, ui};
    Console console{rig.registry, lua};

    ConsoleRig() {
        registerCoreActions(rig.registry, rig.nav, {});
        registerConsoleActions(rig.registry, rig.nav, console);
    }

    [[nodiscard]] std::vector<std::string> lines(ConsoleLine kind) const {
        return console.entries() | std::views::filter([&](const ConsoleEntry& line) { return line.kind == kind; }) |
               std::views::transform(&ConsoleEntry::text) | std::ranges::to<std::vector>();
    }
};

std::string firstLabel(const Completions& found) { return found.items.empty() ? "" : found.items.front().label; }

Completions completeAtEnd(const Registry& registry, std::string_view text) {
    return complete(registry, text, text.size());
}

}  // namespace

TEST_CASE("the console prints, shows values and names the line of an error") {
    ConsoleRig fixture;
    const Json printed = fixture.console.eval("print('hello', 42) return {a = 1}");
    CHECK(printed.at("ok") == true);
    CHECK(fixture.lines(ConsoleLine::Input) == std::vector<std::string>{"> print('hello', 42) return {a = 1}"});
    CHECK(fixture.lines(ConsoleLine::Print) == std::vector<std::string>{"hello\t42"});
    CHECK(fixture.lines(ConsoleLine::Result) == std::vector<std::string>{"{", "  \"a\": 1", "}"});

    CHECK(fixture.console.eval("1 + 2").at("text") == "3");

    const Json failed = fixture.console.eval("local x = 1\nerror('boom')");
    CHECK(failed.at("ok") == false);
    CHECK(fixture.lines(ConsoleLine::Error).back() == "console:2: boom");
    CHECK(fixture.lines(ConsoleLine::Input).back() == ". error('boom')");

    fixture.console.eval("emerick.run('nothing.here')");
    CHECK(fixture.lines(ConsoleLine::Error).back() == "console:1: no action nothing.here");
}

TEST_CASE("a runaway console line stops at the instruction limit") {
    ConsoleRig fixture;
    CHECK(fixture.console.eval("while true do end").at("ok") == false);
    CHECK(fixture.lines(ConsoleLine::Error).back().find("instructions") != std::string::npos);
    CHECK(fixture.console.eval("return 1").at("text") == "1");
}

TEST_CASE("console history persists in settings and steps back and forth") {
    ProjectRig rig;
    {
        UiApi ui{rig.registry};
        LuaHost lua{rig.registry, ui};
        Console console{rig.registry, lua};
        registerConsoleActions(rig.registry, rig.nav, console);
        for (const char* line : {"return 1", "return 2", "return 2", "return 3"}) {
            console.eval(line);
        }
        CHECK(console.history() == std::vector<std::string>{"return 1", "return 2", "return 3"});
    }
    Registry reopened{rig.dir.filePath("settings.ini")};
    UiApi ui{reopened};
    LuaHost lua{reopened, ui};
    Console console{reopened, lua};
    Nav nav;
    registerConsoleActions(reopened, nav, console);
    CHECK(console.history() == std::vector<std::string>{"return 1", "return 2", "return 3"});
    CHECK(console.historyStep(-1, "draft") == "return 3");
    CHECK(console.historyStep(-1, "") == "return 2");
    CHECK(console.historyStep(1, "") == "return 3");
    CHECK(console.historyStep(1, "") == "draft");

    for (int line = 0; line < static_cast<int>(kConsoleHistory) + 5; ++line) {
        console.eval("return " + std::to_string(line));
    }
    CHECK(console.history().size() == kConsoleHistory);
    CHECK(console.history().front() == "return 5");
}

TEST_CASE("completion offers action ids, setting ids, members and tracks by fuzzy match") {
    ConsoleRig fixture;
    fixture.rig.run("track.addAudio", {{"name", "Lead Vocal"}});
    const Registry& registry = fixture.rig.registry;

    const Completions actions = completeAtEnd(registry, "emerick.run(\"trsemu");
    REQUIRE(!actions.items.empty());
    CHECK(actions.items.front().label == "track.setMute");
    CHECK(actions.items.front().insert == "track.setMute\"");
    CHECK(actions.items.front().detail == "Mute track   (track?: string, value?: boolean)");
    CHECK(actions.from == std::string_view{"emerick.run(\""}.size());

    CHECK(firstLabel(completeAtEnd(registry, "emerick.setting('bufsz")) == "audio.bufferSize");
    const Completions closed = complete(registry, "emerick.setting('bufsz')", 22);
    REQUIRE(!closed.items.empty());
    CHECK(closed.items.front().insert == "audio.bufferSize");
    CHECK(firstLabel(completeAtEnd(registry, "x = emerick.ui.fi")) == "ui.find");
    CHECK(firstLabel(completeAtEnd(registry, "emerick.run('track.setMute', {track=\"lead")) == "Lead Vocal");
    CHECK(completeAtEnd(registry, "-- emerick.run(\"tr").items.empty());

    const QVariantMap shown = fixture.console.completions("emerick.setting(\"bufsz", 22);
    CHECK(shown.value("from").toInt() == 17);
    REQUIRE(!shown.value("items").toList().isEmpty());
    CHECK(shown.value("items").toList().front().toMap().value("label") == "audio.bufferSize");
}

TEST_CASE("help prints an action, a setting and an emerick function from the registry") {
    ConsoleRig fixture;
    fixture.console.eval("help('track.setMute')");
    CHECK(fixture.lines(ConsoleLine::Print) == std::vector<std::string>{"Mute track  (track.setMute)",
                                                                        "Parameters: track?: string, value?: boolean",
                                                                        "Shortcut: M"});
    fixture.console.clear();
    fixture.console.eval("help(emerick.run)");
    CHECK(fixture.lines(ConsoleLine::Print).front() == "emerick.run(id, params?)");
    fixture.console.eval("help('audio.bufferSize')");
    CHECK(std::ranges::contains(fixture.lines(ConsoleLine::Print), std::string{"Value: 256"}));
    CHECK(fixture.console.eval("help('nope')").at("ok") == false);
}

TEST_CASE("the action log writes a UI action as the Lua line that runs it again") {
    ConsoleRig fixture;
    const auto id = fixture.rig.run("track.addAudio").at("id").get<std::string>();
    fixture.rig.registry.runAction("track.setMute", {{"track", QString::fromStdString(id)}, {"value", true}});

    const ConsoleEntry& logged = fixture.console.entries().back();
    CHECK(logged.kind == ConsoleLine::Log);
    CHECK(logged.source == ActionSource::Ui);
    CHECK(logged.text == R"(emerick.run("track.setMute", {track="Audio 1", value=true}))");

    fixture.rig.run("track.setMute", {{"track", id}, {"value", false}});
    REQUIRE(!fixture.rig.controller->project().track(model::TrackId{id})->mute);
    const int serial = std::ranges::find(fixture.console.entries(), logged.text, &ConsoleEntry::text)->serial;
    fixture.rig.run("console.runAgain", {{"entry", serial}});
    CHECK(fixture.rig.controller->project().track(model::TrackId{id})->mute);
    const auto info = fixture.lines(ConsoleLine::Log);
    CHECK(info.back() == logged.text);
    CHECK(std::ranges::find(fixture.console.entries() | std::views::reverse, ConsoleLine::Log, &ConsoleEntry::kind)
              ->source == ActionSource::Lua);
}

TEST_CASE("the console colors the Lua of input and action lines") {
    ConsoleRig fixture;
    QTextDocument document;
    fixture.console.attach(&document, {{"input", QColor(Qt::gray)}, {"call", "#00ff00"}, {"actionId", "#ff0000"}});
    fixture.console.eval("help('track.setMute')");
    const QTextBlock input = document.firstBlock();
    REQUIRE(input.text() == "> help('track.setMute')");
    const auto colorAt = [&](int position) {
        QTextCursor cursor(&document);
        cursor.setPosition(input.position() + position + 1);
        return cursor.charFormat().foreground().color();
    };
    CHECK(colorAt(0) == QColor(Qt::gray));
    CHECK(colorAt(2) == QColor("#00ff00"));
    CHECK(colorAt(8) == QColor("#ff0000"));
}

TEST_CASE("the console keeps the last 5000 lines in its stream and its view") {
    ConsoleRig fixture;
    QTextDocument document;
    fixture.console.attach(&document, {});
    fixture.console.eval("for i = 1, 6000 do print(i) end");
    REQUIRE(fixture.console.entries().size() == kConsoleLines);
    CHECK(fixture.console.entries().front().text == "1001");
    CHECK(document.blockCount() == static_cast<int>(kConsoleLines));
    CHECK(document.firstBlock().text() == "1001");
    CHECK(fixture.console.entryAt(0).value("text") == "1001");

    fixture.console.eval("error('late')");
    fixture.console.setFilter(ConsoleFilter::Errors);
    CHECK(document.blockCount() == 1);
    CHECK(document.firstBlock().text() == "console:1: late");
}

TEST_CASE("console.copyAll copies the lines the filter shows as the view shows them") {
    ConsoleRig fixture;
    QTextDocument document;
    fixture.console.attach(&document, {});
    fixture.console.eval("print('one')");
    fixture.console.eval("error('two')");
    fixture.rig.registry.run("view.toggleConsole", Json::object(), ActionSource::Ui);
    CHECK(fixture.rig.run("console.copyAll").at("text") == document.toPlainText().toStdString());
    CHECK(QGuiApplication::clipboard()->text() == document.toPlainText());

    fixture.rig.run("console.setFilter", {{"filter", "errors"}});
    CHECK(fixture.rig.run("console.copyAll").at("text") == "console:1: two");
}

TEST_CASE("console.log reads recent lines over MCP and a server without Lua refuses Lua actions") {
    ConsoleRig fixture;
    fixture.console.eval("print('one')");
    const auto tools = mcpTools(fixture.rig.registry, fixture.ui, nullptr, &fixture.console);
    const auto tool = [&](const std::string& name) { return std::ranges::find(tools, name, &McpTool::name)->call; };
    CHECK(tool("console.log")({{"lines", 1}}) ==
          Json{{"lines", {{{"kind", "print"}, {"text", "one"}, {"source", "code"}}}}});
    CHECK_THROWS_AS(tool("actions.run")({{"id", "console.eval"}, {"params", {{"code", "return 1"}}}}), AppError);
    CHECK(std::ranges::find(tools, "lua.eval", &McpTool::name) == tools.end());
}
