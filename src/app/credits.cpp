#include "emerick/app/credits.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "emerick/app/dexed.hpp"
#include "emerick/app/params.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/track_rows.hpp"
#include "emerick/core/plugin_format.hpp"

namespace emerick::app {

namespace {

constexpr std::string_view kGpl3 = "GPL-3.0";
constexpr std::array<std::pair<std::string_view, std::string_view>, 4> kPluginLicenses{{
    {"org.emerick.", kGpl3},
    {kDexedPluginId, kGpl3},
    {"michaelwillis.dragonfly.", kGpl3},
    {"com.airwindows.consolidated", kGpl3},
}};

Json generalUserCredit() {
    return {{"name", "GeneralUser GS v2.0.3"},
            {"author", "S. Christian Collins"},
            {"license", "GeneralUser GS License v2.0"},
            {"url", "https://github.com/mrbumpy409/GeneralUser-GS"}};
}

Json stateJson(const model::PluginSlot& slot) {
    return Json::parse(slot.state.begin(), slot.state.end(), nullptr, false);
}

std::string stringIn(const Json& object, const std::string& key) {
    return object.is_object() && object.contains(key) && object.at(key).is_string() ? object.at(key).get<std::string>()
                                                                                    : std::string{};
}

template <typename Key>
Json values(const std::map<Key, Json>& entries) {
    return entries | std::views::values | std::ranges::to<Json::array_t>();
}

class CreditList {
public:
    CreditList(const Libraries& libraries)
        : catalog_(libraries.listJson()), folder_(std::filesystem::weakly_canonical(libraries.folder())) {}

    void add(const model::PluginSlot& slot) {
        Json plugin{{"id", slot.pluginId},
                    {"name", slot.name},
                    {"vendor", slot.vendor},
                    {"format", core::pluginFormatName(slot.format).id}};
        if (std::string license = pluginLicense(slot.pluginId); !license.empty()) {
            plugin["license"] = std::move(license);
        }
        plugins_.try_emplace(slot.pluginId + "/" + plugin.at("format").get<std::string>(), std::move(plugin));
        using Credit = void (CreditList::*)(const model::PluginSlot&);
        const std::array<std::pair<std::string_view, Credit>, 4> sounds{
            {{kSamplerPluginId, &CreditList::addSfz},
             {kDexedPluginId, &CreditList::addCartridge},
             {kGeneralMidiPluginId, &CreditList::addSoundFont},
             {kVoicesPluginId, &CreditList::addVoice}}};
        if (const auto* const found =
                std::ranges::find(sounds, slot.pluginId, &std::pair<std::string_view, Credit>::first);
            found != sounds.end()) {
            (this->*found->second)(slot);
        }
    }

    [[nodiscard]] Json json() const {
        return {{"libraries", values(libraries_)},
                {"sfz", sfz_},
                {"cartridges", values(cartridges_)},
                {"soundFonts", values(soundFonts_)},
                {"expansionPacks", expansionPacks_},
                {"plugins", values(plugins_)},
                {"message", std::to_string(libraries_.size()) + " libraries, " + std::to_string(cartridges_.size()) +
                                " cartridges, " + std::to_string(plugins_.size()) + " plugins"}};
    }

private:
    void addSfz(const model::PluginSlot& slot) {
        const std::filesystem::path path = stringIn(stateJson(slot), "path");
        if (path.empty()) {
            return;
        }
        const std::filesystem::path inside = path.lexically_relative(folder_);
        const auto library = inside.empty() || *inside.begin() == ".."
                                 ? catalog_.end()
                                 : std::ranges::find(catalog_, Json(inside.begin()->string()),
                                                     [](const Json& entry) { return entry.at("id"); });
        if (library == catalog_.end()) {
            sfz_.insert(path.string());
            return;
        }
        auto [credit, added] = libraries_.try_emplace(library->at("id").get<std::string>(),
                                                      Json{{"id", library->at("id")},
                                                           {"name", library->at("name")},
                                                           {"description", library->at("description")},
                                                           {"license", library->at("license")},
                                                           {"url", library->at("url")},
                                                           {"programs", Json::array()}});
        const std::string file = inside.lexically_relative(*inside.begin()).generic_string();
        if (!std::ranges::contains(credit->second.at("programs"), Json(file))) {
            credit->second.at("programs").push_back(file);
        }
    }

    void addCartridge(const model::PluginSlot& slot) {
        const auto state = parseDexedState(slot.state);
        if (!state) {
            return;
        }
        Json credit = cartridgeCredit(state->cartridge);
        if (credit.is_null()) {
            credit = {{"name", "unknown cartridge"}};
        }
        auto [entry, added] = cartridges_.try_emplace(credit.at("name").get<std::string>(), credit);
        entry->second["programs"].push_back(dx7ProgramNames(state->cartridge).at(state->program));
    }

    void addSoundFont(const model::PluginSlot& slot) {
        const Json state = stateJson(slot);
        const std::string path =
            state.is_discarded() ? std::string{slot.state.begin(), slot.state.end()} : stringIn(state, "soundFont");
        soundFonts_.try_emplace(
            path, path.empty() ? generalUserCredit()
                               : Json{{"name", std::filesystem::path{path}.filename().string()}, {"path", path}});
    }

    void addVoice(const model::PluginSlot& slot) {
        const Json state = stateJson(slot);
        if (std::string pack =
                state.is_object() && state.contains("voice") ? stringIn(state.at("voice"), "pack") : std::string{};
            !pack.empty()) {
            expansionPacks_.insert(std::move(pack));
        }
    }

    Json catalog_;
    std::filesystem::path folder_;
    std::map<std::string, Json> libraries_;
    std::set<std::string> sfz_;
    std::map<std::string, Json> cartridges_;
    std::map<std::string, Json> soundFonts_;
    std::set<std::string> expansionPacks_;
    std::map<std::string, Json> plugins_;
};

}  // namespace

std::string pluginLicense(std::string_view pluginId) {
    const auto* const found =
        std::ranges::find_if(kPluginLicenses, [&](const auto& entry) { return pluginId.starts_with(entry.first); });
    return found == kPluginLicenses.end() ? std::string{} : std::string{found->second};
}

void registerCreditActions(Registry& registry, ProjectController& controller, const Libraries& libraries) {
    registry.addAction(
        {.id = "project.credits",
         .title = "List project credits",
         .description =
             "Lists the credit of every sound the open project plays: libraries [{id, name, description, license, "
             "url, programs}] with the .sfz files used, sfz paths outside any library, DX7 cartridges [{id, name, "
             "license, url, programs}], General MIDI soundFonts [{name, author, license, url} or {name, path}], "
             "expansionPacks by name, and plugins [{id, name, vendor, format, license}], license only where known.",
         .aliases = {"credits", "attribution", "sample licenses"},
         .category = "Project",
         .run = [&controller, &libraries](const Json&) {
             CreditList credits(libraries);
             for (const model::Track& track : controller.project().document().tracks) {
                 if (track.instrument) {
                     credits.add(*track.instrument);
                 }
                 for (const model::Insert& insert : track.inserts) {
                     if (insert.plugin) {
                         credits.add(*insert.plugin);
                     }
                 }
             }
             return credits.json();
         }});
    registry.addAction(
        {.id = "catalog.export",
         .title = "Export sound catalog",
         .description =
             "Writes every sound Emerick can play as JSON to path, or returns it without path: libraries as "
             "library.list lists them with their programs and General MIDI mappings, plugins as plugins.list lists "
             "them with the license of the ones Emerick ships, the DX7 cartridges Emerick ships with their credits, "
             "and the General MIDI soundFont's credit.",
         .aliases = {"export credits", "export libraries", "sound list"},
         .category = "Export",
         .schema = {{"properties", {{"path", {{"type", "string"}, {"description", "the .json file to write"}}}}}},
         .run =
             [&registry](const Json& params) {
                 Json exported{{"libraries", registry.run("library.list").at("libraries")},
                               {"plugins", registry.run("plugins.list").at("plugins")},
                               {"cartridges", shippedCartridgeCredits()},
                               {"soundFont", generalUserCredit()}};
                 const std::optional<std::string> path = optionalParam<std::string>(params, "path");
                 if (!path) {
                     return exported;
                 }
                 const std::filesystem::path file = registry.checkedPath(*path, PathAccess::Write);
                 std::ofstream out(file);
                 out << exported.dump(2) << '\n';
                 if (!out) {
                     throw AppError("cannot write " + file.string());
                 }
                 return Json{{"file", file.string()}, {"message", "Exported the sound catalog"}};
             },
         .menus = {kFileMenu},
         .dialog = FileDialog{
             .param = "path", .mode = FileDialogMode::Save, .nameFilter = "JSON files (*.json)", .suffix = "json"}});
}

}  // namespace emerick::app
