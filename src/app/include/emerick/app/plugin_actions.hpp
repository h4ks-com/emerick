#pragma once

#include <filesystem>

#include "emerick/app/dexed.hpp"
#include "emerick/app/nav.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/app/track_rows.hpp"

namespace emerick::app {

inline constexpr const char* kClapFoldersSetting = "plugins.clapFolders";
inline constexpr const char* kVst3FoldersSetting = "plugins.vst3Folders";
inline constexpr const char* kGeneralMidiSoundFontSetting = "plugins.generalMidiSoundFont";

// The Plugins settings section with the user's CLAP folders, and the actions that rescan and list CLAP plugins.
void registerPluginActions(Registry& registry, Nav& nav, PluginCatalog& catalog);

// The add instrument track, load instrument, remove instrument and show editor actions. sampler.load plays the sound
// libraries installed in libraryFolder.
void registerInstrumentActions(Registry& registry, ProjectController& controller, Nav& nav, PluginCatalog& catalog,
                               const std::filesystem::path& libraryFolder = {});
// Adds insert.addPlugin, which insert.add runs for a plugin.
void registerPluginInsertActions(Registry& registry, ProjectController& controller, Nav& nav, PluginCatalog& catalog);
// Adds dexed.programs and dexed.load, which pick the DX7 program a Dexed instrument plays.
void registerDexedActions(Registry& registry, ProjectController& controller, Nav& nav, PluginCatalog& catalog);
// The credit {id, name, license, url} of each DX7 cartridge Emerick ships.
Json shippedCartridgeCredits();
// The credit of the cartridge when Emerick ships it, else null.
Json cartridgeCredit(const Dx7Cartridge& cartridge);

// Scans the catalog the first time something asks for its plugins.
const PluginCatalog& scannedCatalog(const Registry& registry, PluginCatalog& catalog);

// The slot of the instrument the params name by plugin id and optional format.
model::PluginSlot slotFor(const Registry& registry, PluginCatalog& catalog, const Json& params);
// The MIDI track the params name, or the selected one.
model::TrackId midiTrack(const Registry& registry, const ProjectController& controller, const Nav& nav,
                         const Json& params);
// A script runs its actions without turning the event loop, so we load the instruments its earlier actions added.
void syncInstruments(ProjectController& controller);

}  // namespace emerick::app
