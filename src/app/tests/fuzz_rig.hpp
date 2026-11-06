#pragma once

#include <QtEnvironmentVariables>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "emerick/app/backends.hpp"
#include "emerick/app/console.hpp"
#include "emerick/app/core_actions.hpp"
#include "emerick/app/lua_host.hpp"
#include "emerick/app/script_library.hpp"
#include "emerick/app/ui_api.hpp"
#include "project_rig.hpp"

namespace emerick::app::test {

// A project rig with every built-in action the app registers.
struct FuzzRig : ProjectRig {
    UiApi ui{registry};
    LuaHost lua{registry, ui};
    Console console{registry, lua};
    std::unique_ptr<ScriptLibrary> scripts;

    FuzzRig() {
        // Hostile paths would otherwise write files wherever the test runs.
        registry.setAgentRoots({.read = std::nullopt, .write = dir.path().toStdString()});
        registerCoreActions(registry, nav, {.audio = "null", .midi = "virtual"});
        registerVirtualMidiActions(registry, *midi);
        registerConsoleActions(registry, nav, console);
        copyExampleScripts(path("scripts"));
        scripts = std::make_unique<ScriptLibrary>(registry, console, path("scripts"));
        registerScriptActions(registry, *scripts, nav);
    }

    // An audio track with a clip on a file in the project, a MIDI track with a clip and a marker.
    void populate() {
        const std::filesystem::path audioFolder = controller->folder() / "audio";
        std::filesystem::create_directories(audioFolder);
        writeConstant(audioFolder / "tone.wav", 48000, 0.25F);
        model::Track vox;
        vox.name = "Vox";
        vox.clips.emplace_back(model::AudioClip{.start = 0, .length = 3840, .file = "audio/tone.wav"});
        model::Track keys;
        keys.name = "Keys";
        keys.kind = "midi";
        keys.clips.emplace_back(
            model::MidiClip{.start = 1920, .length = 3840, .events = {{0, {0x90, 60, 100}}, {480, {0x80, 60, 0}}}});
        controller->project().addTrack(std::move(vox));
        controller->project().addTrack(std::move(keys));
        controller->project().setMarkers({{.name = "Verse", .at = 3840}}, "add marker");
    }
};

// EMERICK_SEED=<n> repeats a run; without it every run draws a new seed.
inline std::uint32_t fuzzSeed() {
    bool given = false;
    const std::uint32_t seed = qEnvironmentVariable("EMERICK_SEED").toUInt(&given);
    return given ? seed : std::random_device{}();
}

// Actions whose effects reach outside the project and its rig folder: the app, the desktop and the clipboard.
inline const std::set<std::string> kSandboxedOut{"app.quit", "project.reveal", "script.openFolder", "console.copyEntry",
                                                 "console.copyAll"};

inline std::vector<Json> hostileValues(const model::Document& document) {
    constexpr std::size_t kLongText = 5000;
    std::vector<Json> values{nullptr,
                             -1,
                             0,
                             1,
                             std::numeric_limits<std::int64_t>::max(),
                             std::numeric_limits<std::int64_t>::min(),
                             std::numeric_limits<std::uint64_t>::max(),
                             1.0e308,
                             -1.0e308,
                             0.5,
                             std::nan(""),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(),
                             true,
                             false,
                             "",
                             "Vox",
                             "1.1.0",
                             "0.0.-5",
                             "99999999.99.99999",
                             std::string(kLongText, 'x'),
                             std::string{"a\0bé\U0001F3B9", 9},
                             "trk-unknown",
                             Json::array(),
                             Json::array({1, "Vox", nullptr, -5}),
                             Json::object(),
                             Json{{"device", 7}, {"channels", "all"}}};
    for (const model::Track& track : document.tracks) {
        values.emplace_back(track.id.value);
        for (const model::Clip& clip : track.clips) {
            values.emplace_back(model::clipId(clip).value);
        }
    }
    for (const model::Marker& marker : document.markers) {
        values.emplace_back(marker.id.value);
    }
    return values;
}

inline std::set<std::string> schemaKeys(const Action& action) {
    std::set<std::string> keys;
    if (action.schema.contains("properties") && action.schema.at("properties").is_object()) {
        for (const auto& [key, value] : action.schema.at("properties").items()) {
            keys.insert(key);
        }
    }
    if (action.schema.contains("required") && action.schema.at("required").is_array()) {
        for (const Json& key : action.schema.at("required")) {
            keys.insert(key.get<std::string>());
        }
    }
    return keys;
}

inline std::vector<std::string> allParamKeys(const Registry& registry) {
    std::set<std::string> keys{
        "track",       "tracks",  "clip",     "clips",  "at",      "start",   "end",       "length",  "value",
        "name",        "path",    "folder",   "db",     "port",    "index",   "confirm",   "channel", "channels",
        "device",      "thru",    "sysex",    "fadeIn", "fadeOut", "color",   "icon",      "bpm",     "numerator",
        "denominator", "audioIn", "audioOut", "midiIn", "midiOut", "enabled", "selection", "code",    "includeEvents",
        "shiftBars",   "query",   "id",       "scope",  "bytes",   "chord",   "section"};
    for (const Action& action : registry.actions()) {
        keys.merge(schemaKeys(action));
    }
    return {keys.begin(), keys.end()};
}

template <class Item>
const Item& pick(const std::vector<Item>& items, std::mt19937& rng) {
    return items[std::uniform_int_distribution<std::size_t>{0, items.size() - 1}(rng)];
}

// Runs the action with params as an agent would and requires success or an AppError that left the document as it was.
inline void runHostile(const ProjectRig& rig, const std::string& id, const Json& params) {
    const model::Document before = rig.controller->project().document();
    const std::string shown = params.dump(-1, ' ', false, Json::error_handler_t::replace);
    try {
        rig.registry.run(id, params, ActionSource::Mcp);
    } catch (const AppError&) {
        if (rig.controller->project().document() != before) {
            FAIL_CHECK(id << " changed the document and then failed with " << shown);
        }
    } catch (const std::exception& error) {
        FAIL_CHECK(id << " threw " << error.what() << " with " << shown);
    }
}

// Every action, on an empty and on a populated project, with no params, with each schema param set to each hostile
// value, and with random hostile params. Each action starts from the same document, and the rest of the app keeps
// whatever the earlier actions left, as it would for an agent.
template <class Rig>
void checkHostileParams(const std::function<bool(const std::string&)>& covers) {
    const std::uint32_t seed = fuzzSeed();
    INFO("EMERICK_SEED=" << seed);
    std::mt19937 rng{seed};
    constexpr int kRandomCalls = 40;
    for (const bool populated : {false, true}) {
        Rig rig;
        if (populated) {
            rig.populate();
        }
        const model::Document start = rig.controller->project().document();
        const std::vector<Json> values = hostileValues(start);
        const std::vector<std::string> keys = allParamKeys(rig.registry);
        std::vector<std::string> ids;
        for (const Action& action : rig.registry.actions()) {
            if (covers(action.id) && !kSandboxedOut.contains(action.id)) {
                ids.push_back(action.id);
            }
        }
        for (const std::string& id : ids) {
            rig.controller->project().reset(start);
            for (const Json& params : {Json::object(), Json(nullptr), Json::array(), Json("Vox"), Json(7)}) {
                runHostile(rig, id, params);
            }
            const Action* action = rig.registry.action(id);
            if (action == nullptr) {
                continue;
            }
            for (const std::string& key : schemaKeys(*action)) {
                for (const Json& value : values) {
                    runHostile(rig, id, {{key, value}});
                }
            }
            for (int call = 0; call < kRandomCalls; ++call) {
                Json params = Json::object();
                for (int field = std::uniform_int_distribution{1, 4}(rng); field > 0; --field) {
                    params[pick(keys, rng)] = pick(values, rng);
                }
                runHostile(rig, id, params);
            }
        }
    }
}

}  // namespace emerick::app::test
