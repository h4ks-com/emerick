#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <ranges>
#include <set>
#include <string>
#include <vector>

#include "emerick/app/console.hpp"
#include "emerick/app/core_actions.hpp"
#include "emerick/app/libraries.hpp"
#include "emerick/app/lua_host.hpp"
#include "emerick/app/monitor_actions.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/script_library.hpp"
#include "emerick/app/ui_api.hpp"
#include "emerick/yamaha/plugin.hpp"
#include "project_rig.hpp"

using namespace emerick::app;

namespace {

// Actions a person cannot reach from a menu, a shortcut or a control yet. Give each one a UI path and remove it from
// this list; never add to it.
const std::set<std::string> kUnreachable = {
    "automation.getLane", "devices.list", "metronome.setVolume",   "project.clipEvents",
    "script.list",        "script.run",   "transport.setClockOut", "yamaha.chart.text",
};

// Listings for agents, which menus leave out.
const std::set<std::string> kAgentOnly = {"dexed.programs"};

std::string qmlSource() {
    std::string source;
    for (const char* folder : {"src/ui", "plugins"}) {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(std::filesystem::path{EMERICK_SOURCE_DIR} / folder)) {
            if (entry.path().extension() == ".qml") {
                std::ifstream file(entry.path());
                source.append(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            }
        }
    }
    return source;
}

// Settings sections and plugin lanes build their ids from a quoted prefix, such as "settings.show." + key or
// actionPrefix + ".move:", so we also accept an id that QML spells in two quoted parts split at a dot.
bool namedInQml(const std::string& id, const std::string& qml) {
    if (qml.contains('"' + id + '"') || qml.contains('"' + id + ':')) {
        return true;
    }
    for (std::size_t dot = id.find('.'); dot != std::string::npos; dot = id.find('.', dot + 1)) {
        const std::string tail = id.substr(dot + 1);
        if (qml.contains('"' + id.substr(0, dot + 1) + '"') ||
            (qml.contains('"' + id.substr(0, dot) + '"') &&
             (qml.contains('"' + tail + '"') || qml.contains("\"." + tail)))) {
            return true;
        }
    }
    return false;
}

bool reachable(const Action& action, const std::string& qml, const Registry& registry) {
    return !action.menus.empty() || !action.shortcut.empty() || !action.extraShortcuts.empty() ||
           namedInQml(action.id, qml) || std::ranges::contains(registry.allSettings(), action.id, &Setting::action) ||
           std::ranges::contains(kInstrumentActions, action.id, &InstrumentAction::actionId) ||
           kAgentOnly.contains(action.id);
}

void withEveryAction(const std::function<void(const Registry&)>& check) {
    test::ProjectRig rig;
    registerCoreActions(rig.registry, rig.nav, {});
    const UiApi ui(rig.registry);
    LuaHost lua(rig.registry, ui);
    Console console(rig.registry, lua);
    registerConsoleActions(rig.registry, rig.nav, console);
    ScriptLibrary scripts(rig.registry, console, rig.path("scripts"));
    registerScriptActions(rig.registry, scripts, rig.nav);
    PluginCatalog plugins({.cacheFolder = rig.path("cache"), .scanner = {}, .folders = {}});
    registerPluginActions(rig.registry, rig.nav, plugins);
    registerInstrumentActions(rig.registry, *rig.controller, rig.nav, plugins);
    const auto yamaha = emerick::yamaha::registerWith(rig.registry, *rig.controller, rig.nav);
    const auto monitors = registerMonitors(rig.registry, *rig.controller, rig.nav);
    const auto libraries = registerLibraries(rig.registry, rig.nav, rig.path("libraries"));
    check(rig.registry);
}

}  // namespace

TEST_CASE("every action has a menu entry, a shortcut or a control") {
    withEveryAction([](const Registry& registry) {
        const std::string qml = qmlSource();
        const auto ids = [&](bool wanted) {
            return registry.actions() | std::views::filter([&](const Action& action) {
                       return reachable(action, qml, registry) == wanted;
                   }) |
                   std::views::transform(&Action::id) | std::ranges::to<std::set>();
        };
        std::set<std::string> unreached;
        std::ranges::set_difference(ids(false), kUnreachable, std::inserter(unreached, unreached.end()));
        std::set<std::string> stale;
        std::ranges::set_intersection(ids(true), kUnreachable, std::inserter(stale, stale.end()));
        CHECK(unreached == std::set<std::string>{});
        CHECK(stale == std::set<std::string>{});
    });
}

TEST_CASE("the palette lists equal matches in menu order, so add lists Add audio track and Add MIDI track first") {
    withEveryAction([](const Registry& registry) {
        const auto ids =
            registry.search("add", 20) | std::views::transform(&SearchHit::id) | std::ranges::to<std::vector>();
        REQUIRE(ids.size() == 20);
        CHECK(ids[0] == "track.addAudio");
        CHECK(ids[1] == "track.addMidi");
        const auto position = [&](const std::string& id) { return std::ranges::find(ids, id) - ids.begin(); };
        CHECK(position("midi.import") < position("yamaha.chord.set"));
    });
}
