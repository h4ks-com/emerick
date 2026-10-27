#pragma once

#include "emerick/app/nav.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::app {

inline constexpr const char* kClapFoldersSetting = "plugins.clapFolders";

// The Plugins settings section with the user's CLAP folders, and the actions that rescan and list CLAP plugins.
void registerPluginActions(Registry& registry, Nav& nav, PluginCatalog& catalog);

// The add instrument track, load instrument, remove instrument and show editor actions.
void registerInstrumentActions(Registry& registry, ProjectController& controller, Nav& nav, PluginCatalog& catalog);

// Scans the catalog the first time something asks for its plugins.
const PluginCatalog& scannedCatalog(const Registry& registry, PluginCatalog& catalog);

}  // namespace emerick::app
