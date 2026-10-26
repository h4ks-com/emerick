#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/project_actions.hpp"

namespace emerick::app {

namespace {

Json pluginSchema() { return {{"type", "string"}, {"description", "a CLAP plugin id from plugins.list"}}; }

model::PluginSlot slotFor(const Registry& registry, PluginCatalog& catalog, const Json& params) {
    const auto id = param<std::string>(params, "plugin");
    const auto found = scannedCatalog(registry, catalog).find(id);
    if (!found) {
        throw AppError("no CLAP plugin " + id + ", run plugins.list for plugin ids");
    }
    return {.pluginId = id, .bundle = found->bundle.string(), .name = found->descriptor.name, .state = {}};
}

std::vector<MenuChoice> instrumentChoices(const Registry& registry, PluginCatalog& catalog) {
    return scannedCatalog(registry, catalog).plugins() | std::views::filter([](const CatalogPlugin& plugin) {
               return std::ranges::contains(plugin.descriptor.features, "instrument");
           }) |
           std::views::transform([](const CatalogPlugin& plugin) {
               return MenuChoice{.id = plugin.descriptor.id,
                                 .title = plugin.descriptor.name,
                                 .params = {{"plugin", plugin.descriptor.id}}};
           }) |
           std::ranges::to<std::vector>();
}

model::TrackId instrumentTrack(const Registry& registry, const ProjectController& controller, const Nav& nav,
                               const Json& params) {
    const model::TrackId id = targetTrack(controller, nav, params);
    const model::Track& track = *controller.project().track(id);
    const TrackType* type = registry.trackType(track.kind);
    if (type == nullptr || !type->routes.instrument) {
        throw AppError("track " + track.name + " is not an instrument track, run track.addInstrument first");
    }
    return id;
}

}  // namespace

void registerInstrumentActions(Registry& registry, ProjectController& controller, Nav& nav, PluginCatalog& catalog) {
    const auto choices = [&registry, &catalog] { return instrumentChoices(registry, catalog); };
    registry.addAction(
        {.id = "track.addInstrument",
         .title = "Add instrument track",
         .description = "Adds a track that plays its MIDI through a CLAP instrument into an audio output.",
         .aliases = {"new instrument track", "add software instrument", "add synth"},
         .category = "Tracks",
         .schema = {{"type", "object"},
                    {"required", {"plugin"}},
                    {"properties",
                     {{"plugin", pluginSchema()},
                      {"name", {{"type", "string"}}},
                      {"midiIn", {{"type", "object"}}},
                      {"audioOut", {{"type", "object"}}}}}},
         .run =
             [&registry, &controller, &nav, &catalog](const Json& params) {
                 return addInstrumentTrack(registry, controller, nav, slotFor(registry, catalog, params), params);
             },
         .menus = {kTrackMenu, kLaneContextMenu},
         .submenu = choices});
    registry.addAction(
        {.id = "instrument.load",
         .title = "Load instrument",
         .description = "Plays the track through another CLAP instrument, which starts from its own defaults.",
         .aliases = {"change instrument", "replace instrument", "pick synth"},
         .category = "Tracks",
         .schema = {{"required", {"plugin"}},
                    {"properties", {{"plugin", pluginSchema()}, {"track", {{"type", "string"}}}}}},
         .run =
             [&registry, &controller, &nav, &catalog](const Json& params) {
                 const model::TrackId id = instrumentTrack(registry, controller, nav, params);
                 const model::PluginSlot slot = slotFor(registry, catalog, params);
                 return editTrack(controller, id, "load " + slot.name,
                                  [&](model::Track& track) { track.instrument = slot; });
             },
         .menus = {kTrackContextMenu},
         .submenu = choices,
         .current =
             [&controller, &nav](const Json& params) {
                 const model::Track* track = controller.project().track(targetTrack(controller, nav, params));
                 return track != nullptr && track->instrument ? track->instrument->pluginId : std::string{};
             }});
    registry.addAction({.id = "instrument.showEditor",
                        .title = "Show instrument editor",
                        .description = "Opens the window of the track's CLAP instrument.",
                        .aliases = {"open plugin window", "plugin gui", "edit instrument"},
                        .category = "Tracks",
                        .schema = {{"properties", {{"track", {{"type", "string"}}}}}},
                        .run =
                            [&registry, &controller, &nav](const Json& params) {
                                const model::TrackId id = instrumentTrack(registry, controller, nav, params);
                                controller.instruments().showEditor(id, controller.project().track(id)->name);
                                return Json{{"track", id.value},
                                            {"editorOpen", controller.instruments().editorOpen(id)}};
                            },
                        .menus = {kTrackContextMenu}});
}

}  // namespace emerick::app
