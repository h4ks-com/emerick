#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "emerick/app/params.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/variant.hpp"
#include "emerick/engine/effects.hpp"
#include "emerick/model/json.hpp"

namespace emerick::app {

namespace {

constexpr int kPresetFormat = 1;
constexpr std::size_t kLongestName = 100;
constexpr const char* kCategory = "Tracks / Presets";
constexpr const char* kSuffix = ".json";

// We share the folder, because copying a path in a handler may throw and a shared pointer copies without throwing.
using Folder = std::shared_ptr<const std::filesystem::path>;

struct Preset {
    double trimDb = 0.0;
    std::vector<model::Insert> inserts;
};

constexpr std::string_view kUnsafeLetters = "/\\:*?\"<>|";
constexpr std::array<std::string_view, 22> kReservedNames{
    "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4", "com5", "com6", "com7",
    "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};

// Windows refuses its device names as file names, with or without an extension.
bool reservedOnWindows(const std::string& name) {
    const std::string stem = name.substr(0, name.find('.')) | std::views::transform([](char letter) {
                                 return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
                             }) |
                             std::ranges::to<std::string>();
    return std::ranges::contains(kReservedNames, stem);
}

// A preset name becomes a file name on every platform, so we refuse one that could leave the folder, hide the file or
// that Windows cannot store.
std::string presetName(const Json& params) {
    const auto name = param<std::string>(params, "name");
    const bool unsafe = std::ranges::any_of(
        name, [](char letter) { return kUnsafeLetters.contains(letter) || static_cast<unsigned char>(letter) < ' '; });
    if (name.empty() || name.size() > kLongestName || name.starts_with('.') || name.ends_with('.') ||
        name.ends_with(' ') || unsafe || reservedOnWindows(name)) {
        throw AppError(
            "name takes a preset name of up to 100 characters without / \\ : * ? \" < > |, a leading dot, a trailing "
            "dot or space, or a Windows device name such as CON");
    }
    return name;
}

std::filesystem::path presetPath(const std::filesystem::path& folder, const std::string& name) {
    return folder / (name + kSuffix);
}

std::vector<std::string> presetNames(const std::filesystem::path& folder) {
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        return {};
    }
    auto names = std::filesystem::directory_iterator(folder, error) |
                 std::views::filter([](const std::filesystem::directory_entry& entry) {
                     return entry.path().extension() == kSuffix && !entry.path().stem().string().starts_with('.');
                 }) |
                 std::views::transform(
                     [](const std::filesystem::directory_entry& entry) { return entry.path().stem().string(); }) |
                 std::ranges::to<std::vector>();
    std::ranges::sort(names);
    return names;
}

model::TrackId trackWithInserts(const Registry& registry, const ProjectController& controller, const Nav& nav,
                                const Json& params) {
    const model::TrackId id = targetTrack(controller, nav, params);
    const model::Track& track = *controller.project().track(id);
    const TrackType* type = registry.trackType(track.kind);
    if (type == nullptr || !playsAudio(*type, track)) {
        throw AppError(playsNoAudio(type, track, "use presets on an audio track, an instrument track or a bus"));
    }
    return id;
}

Json presetJson(const model::Track& track) {
    return {{"format", kPresetFormat},
            {"trimDb", track.trimDb},
            {"inserts", track.inserts | std::views::transform(model::insertToJson) | std::ranges::to<Json::array_t>()}};
}

Preset readPreset(const std::filesystem::path& path, const std::string& name) {
    std::ifstream file(path);
    if (!file) {
        throw AppError("no preset " + name + ", run track.presets for their names");
    }
    try {
        const Json json = Json::parse(file);
        if (json.value("format", 0) != kPresetFormat) {
            throw std::runtime_error("format " + json.value("format", Json{}).dump() + " is not one we read");
        }
        return {.trimDb = std::clamp(json.value("trimDb", 0.0), -kMaxTrimDb, kMaxTrimDb),
                .inserts =
                    json.at("inserts") | std::views::transform(model::insertFromJson) | std::ranges::to<std::vector>()};
    } catch (const Json::exception& error) {
        throw AppError("preset " + name + " is damaged: " + error.what());
    } catch (const std::runtime_error& error) {
        throw AppError("preset " + name + " is damaged: " + error.what());
    }
}

// The name of the effect or plugin the insert plays when this app cannot play it.
std::optional<std::string> missingFrom(const Registry& registry, PluginCatalog& catalog, model::Insert& insert) {
    if (!insert.plugin) {
        return engine::findEffect(insert.type) == nullptr ? std::optional{insert.type} : std::nullopt;
    }
    const auto found = scannedCatalog(registry, catalog).find(insert.plugin->pluginId, insert.plugin->format);
    if (!found || !engine::isEffect(found->descriptor)) {
        return insert.plugin->name;
    }
    insert.plugin->bundle = found->bundle.string();
    return std::nullopt;
}

std::string joined(const std::vector<std::string>& names) {
    std::string text;
    for (const std::string& name : names) {
        text += (text.empty() ? "" : ", ") + name;
    }
    return text;
}

Json presetsResult(const std::filesystem::path& folder) {
    const std::vector<std::string> names = presetNames(folder);
    return {{"presets", names}, {"folder", folder.string()}, {"message", std::to_string(names.size()) + " presets"}};
}

void registerSavePreset(Registry& registry, ProjectController& controller, Nav& nav, const Folder& folder) {
    registry.addAction(
        {.id = "track.savePreset",
         .title = "Save preset",
         .description = "Saves the track's inserts with their settings and plugin states, and its trim, as a named "
                        "channel strip preset in the user presets folder. A preset of that name is replaced.",
         .aliases = {"save channel strip setting", "save insert chain", "save strip preset"},
         .category = kCategory,
         .schema = {{"required", {"name"}},
                    {"properties", {{"track", {{"type", "string"}}}, {"name", {{"type", "string"}}}}}},
         .run = [&registry, &controller, &nav, folder](const Json& params) {
             const model::TrackId id = trackWithInserts(registry, controller, nav, params);
             const std::string name = presetName(params);
             syncInstruments(controller);
             model::Document track;
             track.tracks = {*controller.project().track(id)};
             const Json preset = presetJson(controller.instruments().withStates(std::move(track)).tracks.front());
             std::error_code error;
             std::filesystem::create_directories(*folder, error);
             if (error) {
                 throw AppError("cannot create the presets folder " + folder->string() + ": " + error.message());
             }
             try {
                 model::writeJsonFile(preset, presetPath(*folder, name));
             } catch (const std::runtime_error& failure) {
                 throw AppError("cannot save preset " + name + ": " + failure.what());
             }
             Json result =
                 editTrack(controller, id, "name preset", [&](model::Track& edited) { edited.preset = name; });
             result["message"] = "Saved preset " + name;
             return result;
         }});
}

void registerLoadPreset(Registry& registry, ProjectController& controller, Nav& nav, PluginCatalog& catalog,
                        const Folder& folder) {
    registry.addAction(
        {.id = "track.loadPreset",
         .title = "Load preset",
         .description = "Replaces the track's inserts and trim with a channel strip preset as one undo step. Plugins "
                        "that are not installed are left out and named in skipped.",
         .aliases = {"load channel strip setting", "load insert chain", "recall strip preset"},
         .category = kCategory,
         .schema = {{"required", {"name"}},
                    {"properties", {{"track", {{"type", "string"}}}, {"name", {{"type", "string"}}}}}},
         .run =
             [&registry, &controller, &nav, &catalog, folder](const Json& params) {
                 const model::TrackId id = trackWithInserts(registry, controller, nav, params);
                 const std::string name = presetName(params);
                 Preset preset = readPreset(presetPath(*folder, name), name);
                 std::vector<std::string> skipped;
                 std::vector<model::Insert> inserts;
                 for (model::Insert& insert : preset.inserts) {
                     insert.id = model::InsertId::make();
                     if (const auto missing = missingFrom(registry, catalog, insert)) {
                         skipped.push_back(*missing);
                     } else {
                         inserts.push_back(std::move(insert));
                     }
                 }
                 Json result = editTrack(controller, id, "load preset " + name, [&](model::Track& edited) {
                     edited.inserts = inserts;
                     edited.trimDb = preset.trimDb;
                     edited.preset = name;
                 });
                 syncInstruments(controller);
                 result["skipped"] = skipped;
                 if (skipped.empty()) {
                     result["message"] = "Loaded preset " + name;
                     return result;
                 }
                 result["message"] =
                     "Loaded preset " + name + " without " + joined(skipped) + ", which this app cannot play";
                 emit registry.noticeRaised(qs(registry.action("track.loadPreset")->title),
                                            qs(result.at("message").get<std::string>()), {});
                 return result;
             },
         .submenu =
             [folder] {
                 return presetNames(*folder) | std::views::transform([](const std::string& name) {
                            return MenuChoice{.id = name, .title = name, .params = {{"name", name}}};
                        }) |
                        std::ranges::to<std::vector>();
             }});
}

}  // namespace

void registerTrackPresetActions(Registry& registry, ProjectController& controller, Nav& nav, PluginCatalog& catalog,
                                const std::filesystem::path& folder) {
    const auto shared = std::make_shared<const std::filesystem::path>(folder);
    registerSavePreset(registry, controller, nav, shared);
    registerLoadPreset(registry, controller, nav, catalog, shared);
    registry.addAction({.id = "track.presets",
                        .title = "List presets",
                        .description = "Lists the channel strip presets track.loadPreset loads.",
                        .aliases = {"channel strip settings", "strip presets"},
                        .category = kCategory,
                        .run = [shared](const Json&) { return presetsResult(*shared); }});
}

}  // namespace emerick::app
