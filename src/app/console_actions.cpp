#include <QClipboard>
#include <QGuiApplication>
#include <string>

#include "emerick/app/console.hpp"
#include "emerick/app/params.hpp"

namespace emerick::app {

namespace {

constexpr const char* kConsoleCategory = "Console";

Json entrySchema() { return {{"required", {"entry"}}, {"properties", {{"entry", {{"type", "integer"}}}}}}; }

}  // namespace

void registerConsoleActions(Registry& registry, Nav& nav, Console& console) {
    registry.addSetting({.id = kConsoleHistorySetting,
                         .title = "Console history",
                         .category = "Settings / Console",
                         .type = SettingType::List,
                         .defaultValue = Json::array(),
                         .hint = "The last 500 lines run in the console"});
    registry.addLocation({.id = "console", .reveal = [&nav] { nav.setConsoleOpen(true); }});

    registry.addAction({.id = "view.toggleConsole",
                        .title = "Show or hide console",
                        .aliases = {"lua console", "command line", "repl", "action log"},
                        .category = "View",
                        .shortcut = "Ctrl+`",
                        .run =
                            [&nav](const Json&) {
                                nav.setConsoleOpen(!nav.consoleOpen());
                                return Json{{"consoleOpen", nav.consoleOpen()}};
                            },
                        .menus = {kViewMenu}});
    registry.addAction(
        {.id = "console.eval",
         .title = "Run Lua in the console",
         .aliases = {"lua", "evaluate"},
         .category = kConsoleCategory,
         .schema = {{"required", {"code"}}, {"properties", {{"code", {{"type", "string"}}}}}},
         .run = [&console](const Json& params) { return console.eval(param<std::string>(params, "code")); },
         .location = "console",
         .quiet = true,
         .runsLua = true});
    registry.addAction({.id = "console.clear",
                        .title = "Clear console",
                        .category = kConsoleCategory,
                        .shortcut = "Ctrl+L",
                        .run =
                            [&console](const Json&) {
                                console.clear();
                                return Json::object();
                            },
                        .location = "console",
                        .quiet = true});
    registry.addAction(
        {.id = "console.setFilter",
         .title = "Filter console lines",
         .aliases = {"show info", "show errors", "show output"},
         .category = kConsoleCategory,
         .schema = {{"required", {"filter"}},
                    {"properties", {{"filter", {{"type", "string"}, {"enum", {"all", "info", "output", "errors"}}}}}}},
         .run =
             [&console](const Json& params) {
                 const auto key = param<std::string>(params, "filter");
                 const auto filter = enumFromKey<ConsoleFilter>(key);
                 if (!filter) {
                     throw AppError("filter is all, info, output or errors, got " + key);
                 }
                 console.setFilter(*filter);
                 return Json{{"filter", key}};
             },
         .location = "console",
         .quiet = true});
    registry.addAction({.id = "console.copyEntry",
                        .title = "Copy console line",
                        .category = kConsoleCategory,
                        .schema = entrySchema(),
                        .run =
                            [&console](const Json& params) {
                                const std::string text = console.entry(param<int>(params, "entry")).text;
                                QGuiApplication::clipboard()->setText(qs(text));
                                return Json{{"text", text}};
                            },
                        .quiet = true});
    registry.addAction({.id = "console.runAgain",
                        .title = "Run console line again",
                        .category = kConsoleCategory,
                        .schema = entrySchema(),
                        .run =
                            [&console](const Json& params) {
                                const ConsoleEntry& line = console.entry(param<int>(params, "entry"));
                                if (line.kind != ConsoleLine::Log) {
                                    throw AppError("only action log lines run again");
                                }
                                return console.eval(std::string{line.text});
                            },
                        .quiet = true,
                        .runsLua = true});
}

}  // namespace emerick::app
