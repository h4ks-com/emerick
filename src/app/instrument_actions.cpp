#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/voice_audition.hpp"
#include "sampler_params.hpp"

namespace emerick::app {

namespace {

Json pluginSchema() { return {{"type", "string"}, {"description", "a CLAP or VST3 instrument id from plugins.list"}}; }

Json formatSchema() { return {{"type", "string"}, {"enum", {"clap", "vst3"}}}; }

model::PluginSlot slotFor(const Registry& registry, PluginCatalog& catalog, const Json& params) {
    const auto id = param<std::string>(params, "plugin");
    const auto formatId = optionalParam<std::string>(params, "format");
    const auto format = formatId ? core::pluginFormatFromId(*formatId) : std::nullopt;
    if (formatId && !format) {
        throw AppError("format takes clap or vst3, not " + *formatId);
    }
    const auto found = scannedCatalog(registry, catalog).find(id, format);
    if (!found) {
        throw AppError("no plugin " + id + ", run plugins.list for the ids of instruments");
    }
    model::PluginSlot slot{.pluginId = id,
                           .bundle = found->bundle.string(),
                           .name = found->descriptor.name,
                           .state = {},
                           .format = found->descriptor.format};
    if (id == kGeneralMidiPluginId) {
        const auto soundFont = registry.settingValue(kGeneralMidiSoundFontSetting).get<std::string>();
        slot.state.assign(soundFont.begin(), soundFont.end());
    }
    return slot;
}

std::vector<MenuChoice> instrumentChoices(const Registry& registry, PluginCatalog& catalog) {
    return scannedCatalog(registry, catalog).plugins() |
           std::views::filter([](const CatalogPlugin& plugin) { return engine::isInstrument(plugin.descriptor); }) |
           std::views::transform([](const CatalogPlugin& plugin) {
               return MenuChoice{.id = plugin.descriptor.id,
                                 .title = plugin.descriptor.name,
                                 .params = {{"plugin", plugin.descriptor.id},
                                            {"format", core::pluginFormatName(plugin.descriptor.format).id}}};
           }) |
           std::ranges::to<std::vector>();
}

model::TrackId midiTrack(const Registry& registry, const ProjectController& controller, const Nav& nav,
                         const Json& params) {
    const model::TrackId id = targetTrack(controller, nav, params);
    const model::Track& track = *controller.project().track(id);
    const TrackType* type = registry.trackType(track.kind);
    if (type == nullptr || !type->routes.midi) {
        throw AppError("track " + track.name + " is not a MIDI track, select a MIDI track or run track.addInstrument");
    }
    return id;
}

model::TrackId instrumentTrack(const Registry& registry, const ProjectController& controller, const Nav& nav,
                               const Json& params) {
    const model::TrackId id = midiTrack(registry, controller, nav, params);
    const model::Track& track = *controller.project().track(id);
    if (!track.instrument) {
        throw AppError("track " + track.name + " has no instrument, load an instrument on this MIDI track first");
    }
    return id;
}

// A script runs its actions without turning the event loop, so we load the instruments its earlier actions added.
void syncInstruments(ProjectController& controller) { controller.instruments().sync(controller.project().document()); }

std::shared_ptr<engine::Plugin> samplerOf(const Instruments& instruments, const model::Track& track) {
    return track.instrument && track.instrument->pluginId == kSamplerPluginId ? instruments.find(track.id) : nullptr;
}

Json loadSampler(const Registry& registry, ProjectController& controller, PluginCatalog& catalog,
                 const model::TrackId& id, const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw AppError("no SFZ file at " + path.string() + ", pass the path of an .sfz file");
    }
    const std::string name = path.stem().string();
    const std::string state = Json{{"path", path.string()}}.dump();
    controller.project().group("load SFZ " + name, [&] {
        const model::Track& track = *controller.project().track(id);
        if (!track.instrument || track.instrument->pluginId != kSamplerPluginId) {
            loadInstrument(registry, controller, id, slotFor(registry, catalog, {{"plugin", kSamplerPluginId}}));
        }
        editTrack(controller, id, "load SFZ " + name,
                  [&](model::Track& edited) { edited.instrument->state.assign(state.begin(), state.end()); });
    });
    syncInstruments(controller);
    return {{"track", id.value}, {"path", path.string()}, {"message", "Loaded " + name}};
}

// Emerick Sampler names why it plays nothing and whether it still loads in hidden params, which we show as the track's
// warning and wait for in project.waitReady.
void reportSamplers(Registry& registry, const ProjectController& controller) {
    registry.addWarnings([&controller](const model::Document& document) {
        std::vector<engine::SessionError> warnings;
        for (const model::Track& track : document.tracks) {
            const auto sampler = samplerOf(controller.instruments(), track);
            if (std::string text = sampler ? sampler->paramText(sampler::kProblemParam, 0.0) : ""; !text.empty()) {
                warnings.push_back({.trackId = track.id.value, .message = std::move(text)});
            }
        }
        return warnings;
    });
    registry.addLoadingTracks([&controller](const model::Document& document) {
        return document.tracks | std::views::filter([&](const model::Track& track) {
                   const auto sampler = samplerOf(controller.instruments(), track);
                   return sampler && sampler->paramValue(sampler::kLoadingParam) == 1.0;
               }) |
               std::views::transform(&model::Track::id) | std::ranges::to<std::vector>();
    });
}

}  // namespace

void registerInstrumentActions(Registry& registry, ProjectController& controller, Nav& nav, PluginCatalog& catalog) {
    const auto choices = [&registry, &catalog] { return instrumentChoices(registry, catalog); };
    registry.addAction(
        {.id = "track.addInstrument",
         .title = "Add instrument track",
         .description =
             "Adds a MIDI track with a CLAP or VST3 instrument loaded, which plays its MIDI into an audio output.",
         .aliases = {"new instrument track", "add software instrument", "add synth"},
         .category = "Tracks",
         .schema = {{"type", "object"},
                    {"required", {"plugin"}},
                    {"properties",
                     {{"plugin", pluginSchema()},
                      {"format", formatSchema()},
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
         .description =
             "Plays the MIDI track through a CLAP or VST3 instrument into an audio output. The instrument starts "
             "from its own defaults.",
         .aliases = {"change instrument", "replace instrument", "pick synth"},
         .category = "Tracks",
         .schema = {{"required", {"plugin"}},
                    {"properties",
                     {{"plugin", pluginSchema()}, {"format", formatSchema()}, {"track", {{"type", "string"}}}}}},
         .run =
             [&registry, &controller, &nav, &catalog](const Json& params) {
                 const model::TrackId id = midiTrack(registry, controller, nav, params);
                 return loadInstrument(registry, controller, id, slotFor(registry, catalog, params));
             },
         .menus = {kTrackContextMenu},
         .submenu = choices,
         .current =
             [&controller, &nav](const Json& params) {
                 const model::Track* track = controller.project().track(targetTrack(controller, nav, params));
                 return track != nullptr && track->instrument ? track->instrument->pluginId : std::string{};
             }});
    registry.addAction(
        {.id = "instrument.remove",
         .title = "Remove instrument",
         .description = "Clears the MIDI track's instrument, so the track plays only to its MIDI output.",
         .aliases = {"unload instrument", "clear instrument", "no instrument"},
         .category = "Tracks",
         .schema = {{"properties", {{"track", {{"type", "string"}}}}}},
         .run =
             [&registry, &controller, &nav](const Json& params) {
                 const model::TrackId id = instrumentTrack(registry, controller, nav, params);
                 return editTrack(controller, id, "remove instrument",
                                  [](model::Track& track) { track.instrument.reset(); });
             },
         .menus = {kTrackContextMenu}});
    registry.addAction({.id = "instrument.showEditor",
                        .title = "Show instrument editor",
                        .description = "Opens the window of the track's instrument.",
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
                        .menus = {kTrackContextMenu},
                        .windowOnly = true});
    registry.addAction({.id = "instrument.params",
                        .title = "List instrument settings",
                        .description =
                            "Lists the params of the track's CLAP instrument with their values, and the label of every "
                            "choice of a stepped param, such as each General MIDI sound.",
                        .aliases = {"plugin params", "instrument parameters", "list sounds"},
                        .category = "Tracks",
                        .schema = {{"properties", {{"track", {{"type", "string"}}}}}},
                        .run =
                            [&registry, &controller, &nav](const Json& params) {
                                const model::TrackId id = instrumentTrack(registry, controller, nav, params);
                                syncInstruments(controller);
                                Json described = controller.instruments().describe(*controller.project().track(id));
                                if (described.is_null()) {
                                    throw AppError("the instrument of track " + controller.project().track(id)->name +
                                                   " is not loaded, see the track's warning");
                                }
                                described["track"] = id.value;
                                described["message"] = std::to_string(described.at("params").size()) + " settings";
                                return described;
                            },
                        .menus = {kTrackContextMenu},
                        .notifies = true});
    registry.addAction(
        {.id = "instrument.setParam",
         .title = "Set instrument setting",
         .description = "Sets a param of the track's CLAP instrument, such as the General MIDI sound, as one undo "
                        "step. A stepped param also takes the text of one of its choices, such as \"Clavi\", exact "
                        "or the only case-insensitive match. "
                        "The instrument's saved state keeps the value.",
         .aliases = {"pick sound", "change sound", "set plugin param"},
         .category = "Tracks",
         .schema = {{"required", {"param", "value"}},
                    {"properties",
                     {{"track", {{"type", "string"}}},
                      {"param", {{"type", "integer"}, {"description", "a param id from instrument.params"}}},
                      {"value", {{"description", "a number, or the text of one of the param's choices"}}}}}},
         .run = [&registry, &controller, &nav](const Json& params) {
             const model::TrackId id = instrumentTrack(registry, controller, nav, params);
             const auto paramId = param<std::uint32_t>(params, "param");
             const ParamValue value = params.contains("value") && params.at("value").is_string()
                                          ? ParamValue{params.at("value").get<std::string>()}
                                          : ParamValue{param<double>(params, "value")};
             syncInstruments(controller);
             ParamSet set = controller.instruments().setParam(id, paramId, value);
             editTrack(controller, id, "set instrument setting",
                       [&set](model::Track& track) { track.instrument->state = std::move(set.state); });
             return Json{{"track", id.value}, {"param", paramId}, {"value", set.value}, {"text", set.text}};
         }});
    registry.addAction(
        {.id = "sampler.load",
         .title = "Load SFZ instrument",
         .description = "Plays an SFZ instrument file on the MIDI track through Emerick Sampler, loading Emerick "
                        "Sampler first when the track plays another instrument, as one undo step. The project keeps "
                        "the file's path.",
         .aliases = {"open sfz", "load sfz", "sampler file"},
         .category = "Tracks",
         .schema = {{"required", {"path"}},
                    {"properties",
                     {{"track", {{"type", "string"}}},
                      {"path", {{"type", "string"}, {"description", "an .sfz file"}}}}}},
         .run =
             [&registry, &controller, &nav, &catalog](const Json& params) {
                 const model::TrackId id = midiTrack(registry, controller, nav, params);
                 return loadSampler(registry, controller, catalog, id,
                                    registry.checkedPath(param<std::string>(params, "path"), PathAccess::Read));
             },
         .menus = {kTrackContextMenu},
         .dialog = FileDialog{.param = "path", .nameFilter = "SFZ instruments (*.sfz)"}});
    reportSamplers(registry, controller);
    registerAuditionAction(registry, controller);
    registerPluginInsertActions(registry, controller, nav, catalog);
}

}  // namespace emerick::app
