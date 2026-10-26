#include "emerick/app/plugin_actions.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/core_actions.hpp"

namespace emerick::app {

namespace {

std::vector<std::filesystem::path> userFolders(const Registry& registry) {
    return registry.settingValue(kClapFoldersSetting).get<std::vector<std::string>>() |
           std::ranges::to<std::vector<std::filesystem::path>>();
}

Json catalogJson(const PluginCatalog& catalog) {
    const std::vector<CatalogPlugin> plugins = catalog.plugins();
    const auto blocked = catalog.blocked();
    return {{"plugins", plugins | std::views::transform([](const CatalogPlugin& plugin) {
                            const engine::ClapDescriptor& described = plugin.descriptor;
                            return Json{{"id", described.id},
                                        {"name", described.name},
                                        {"vendor", described.vendor},
                                        {"version", described.version},
                                        {"features", described.features},
                                        {"instrument", std::ranges::contains(described.features, "instrument")},
                                        {"bundle", plugin.bundle.string()}};
                        }) | std::ranges::to<Json::array_t>()},
            {"blocked", blocked | std::views::transform([](const auto& entry) {
                            return Json{{"bundle", entry.first.string()}, {"reason", entry.second}};
                        }) | std::ranges::to<Json::array_t>()},
            {"message", "Found " + std::to_string(plugins.size()) + " CLAP plugins, " + std::to_string(blocked.size()) +
                            " bundles blocked"}};
}

}  // namespace

const PluginCatalog& scannedCatalog(const Registry& registry, PluginCatalog& catalog) {
    if (!catalog.scanned()) {
        catalog.scan(userFolders(registry), false);
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
                         .action = "plugins.rescan"});
    registry.addAction({.id = "plugins.rescan",
                        .title = "Rescan plugins",
                        .description = "Scans every CLAP folder again, including bundles that crashed or hung before.",
                        .aliases = {"scan plugins", "find plugins", "refresh plugins"},
                        .category = "Plugins",
                        .run =
                            [&registry, &catalog](const Json&) {
                                catalog.scan(userFolders(registry), true);
                                return catalogJson(catalog);
                            },
                        .menus = {kPluginsMenu},
                        .notifies = true});
    registry.addAction(
        {.id = "plugins.list",
         .title = "List plugins",
         .description = "Lists the CLAP plugins found and the bundles blocked because they crashed, "
                        "hung or failed to load.",
         .aliases = {"installed plugins", "clap plugins"},
         .category = "Plugins",
         .run = [&registry, &catalog](const Json&) { return catalogJson(scannedCatalog(registry, catalog)); },
         .menus = {kPluginsMenu},
         .notifies = true});
}

}  // namespace emerick::app
