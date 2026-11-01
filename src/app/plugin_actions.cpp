#include "emerick/app/plugin_actions.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/core_actions.hpp"

namespace emerick::app {

namespace {

std::vector<std::filesystem::path> userFolders(const Registry& registry) {
    std::vector<std::string> folders = registry.settingValue(kClapFoldersSetting).get<std::vector<std::string>>();
    std::ranges::copy(registry.settingValue(kVst3FoldersSetting).get<std::vector<std::string>>(),
                      std::back_inserter(folders));
    return folders | std::ranges::to<std::vector<std::filesystem::path>>();
}

std::vector<std::filesystem::path> scanFolders(const Registry& registry) {
    return registry.limits().bundledPluginsOnly ? std::vector<std::filesystem::path>{} : userFolders(registry);
}

std::string countOf(const std::vector<CatalogPlugin>& plugins, core::PluginFormat format) {
    const auto count =
        std::ranges::count(plugins, format, [](const CatalogPlugin& plugin) { return plugin.descriptor.format; });
    return std::to_string(count) + " " + std::string{core::pluginFormatName(format).title};
}

Json catalogJson(const PluginCatalog& catalog) {
    const std::vector<CatalogPlugin> plugins = catalog.plugins();
    const auto blocked = catalog.blocked();
    return {{"plugins", plugins | std::views::transform([](const CatalogPlugin& plugin) {
                            const engine::PluginDescriptor& described = plugin.descriptor;
                            return Json{{"id", described.id},
                                        {"format", core::pluginFormatName(described.format).id},
                                        {"name", described.name},
                                        {"vendor", described.vendor},
                                        {"version", described.version},
                                        {"features", described.features},
                                        {"instrument", engine::isInstrument(described)},
                                        {"effect", engine::isEffect(described)},
                                        {"bundle", plugin.bundle.string()}};
                        }) | std::ranges::to<Json::array_t>()},
            {"blocked", blocked | std::views::transform([](const auto& entry) {
                            return Json{{"bundle", entry.first.string()}, {"reason", entry.second}};
                        }) | std::ranges::to<Json::array_t>()},
            {"message", "Found " + countOf(plugins, core::PluginFormat::clap) + " and " +
                            countOf(plugins, core::PluginFormat::vst3) + " plugins, " + std::to_string(blocked.size()) +
                            " bundles blocked"}};
}

}  // namespace

const PluginCatalog& scannedCatalog(const Registry& registry, PluginCatalog& catalog) {
    if (!catalog.scanned()) {
        catalog.scan(scanFolders(registry), false);
    }
    return catalog;
}

void registerPluginActions(Registry& registry, Nav& nav, PluginCatalog& catalog) {
    addSettingsSection(registry, nav, {.key = "plugins", .title = "Plugins"});
    registry.addSetting({.id = kClapFoldersSetting,
                         .title = "CLAP folders",
                         .aliases = {"plugin folders", "clap path"},
                         .category = "Settings / Plugins",
                         .type = SettingType::List,
                         .defaultValue = Json::array(),
                         .hint = "Folders to search for CLAP plugins besides the standard ones",
                         .location = "settings.plugins",
                         .merge = readableFolders(registry),
                         .action = "plugins.rescan"});
    registry.addSetting({.id = kVst3FoldersSetting,
                         .title = "VST3 folders",
                         .aliases = {"vst folders", "vst3 path"},
                         .category = "Settings / Plugins",
                         .type = SettingType::List,
                         .defaultValue = Json::array(),
                         .hint = "Folders to search for VST3 plugins besides the standard ones",
                         .location = "settings.plugins",
                         .merge = readableFolders(registry),
                         .action = "plugins.rescan"});
    addPathSetting(registry,
                   {.id = kGeneralMidiSoundFontSetting,
                    .title = "General MIDI SoundFont",
                    .aliases = {"sf2", "soundfont", "gm sounds"},
                    .category = "Settings / Plugins",
                    .type = SettingType::String,
                    .defaultValue = "",
                    .hint = "Path of the .sf2 that new General MIDI instruments play; leave empty for GeneralUser GS",
                    .location = "settings.plugins"},
                   FileDialogMode::Open, "SoundFonts (*.sf2)");
    registry.addAction(
        {.id = "plugins.rescan",
         .title = "Rescan plugins",
         .description = "Scans every CLAP and VST3 folder again, including bundles that crashed or hung before.",
         .aliases = {"scan plugins", "find plugins", "refresh plugins"},
         .category = "Plugins",
         .run =
             [&registry, &catalog](const Json&) {
                 if (registry.limits().bundledPluginsOnly && !userFolders(registry).empty()) {
                     throw AppError(
                         "this run scans only the plugins the app ships, so clear the CLAP and VST3 "
                         "folders settings");
                 }
                 catalog.scan(scanFolders(registry), true);
                 return catalogJson(catalog);
             },
         .menus = {kPluginsMenu},
         .notifies = true});
    registry.addAction(
        {.id = "plugins.list",
         .title = "List plugins",
         .description = "Lists the CLAP and VST3 plugins found and the bundles blocked because they crashed, "
                        "hung or failed to load.",
         .aliases = {"installed plugins", "clap plugins", "vst3 plugins"},
         .category = "Plugins",
         .run = [&registry, &catalog](const Json&) { return catalogJson(scannedCatalog(registry, catalog)); },
         .menus = {kPluginsMenu},
         .notifies = true});
}

}  // namespace emerick::app
