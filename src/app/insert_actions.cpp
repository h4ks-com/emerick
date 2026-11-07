#include <algorithm>
#include <cmath>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/instruments.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/engine/effects.hpp"

namespace emerick::app {

namespace {

constexpr const char* kCategory = "Tracks / Inserts";

struct InsertRef {
    model::TrackId track;
    model::InsertId insert;
};

// An automation target names a param as "<insert id>.<param id>", so we accept that in place of the insert param.
InsertRef insertParam(const ProjectController& controller, const Json& params) {
    std::string id;
    if (const auto named = optionalParam<std::string>(params, "insert")) {
        id = *named;
    } else if (const auto target = optionalParam<std::string>(params, "param"); target && target->contains('.')) {
        id = target->substr(0, target->find('.'));
    } else {
        throw AppError("missing param insert");
    }
    const model::Track* track = model::trackWithInsert(controller.project().document(), model::InsertId{id});
    if (track == nullptr) {
        throw AppError("no insert " + id + ", read the track's inserts from state.get");
    }
    return {track->id, model::InsertId{id}};
}

// A plugin's param name may hold a dot, so we strip only the insert id the target starts with.
std::string paramName(const Json& params, const InsertRef& ref) {
    const auto name = param<std::string>(params, "param");
    const std::string prefix = ref.insert.value + ".";
    return name.starts_with(prefix) ? name.substr(prefix.size()) : name;
}

model::Insert& insertIn(model::Track& track, const model::InsertId& id) {
    return *std::ranges::find(track.inserts, id, &model::Insert::id);
}

const model::Insert& insertOf(const ProjectController& controller, const InsertRef& ref) {
    const model::Track& track = *controller.project().track(ref.track);
    return *std::ranges::find(track.inserts, ref.insert, &model::Insert::id);
}

// A plugin insert's param values live in its plugin, so we read them from the live plugin by name.
Json insertResult(const ProjectController& controller, const InsertRef& ref) {
    Json result = insertState(insertOf(controller, ref));
    result["track"] = ref.track.value;
    if (const auto plugin = controller.instruments().find(ref.insert)) {
        for (const engine::PluginParam& param : plugin->params()) {
            result["params"][param.name] = plugin->paramValue(param.id).value_or(param.defaultValue);
        }
    }
    return result;
}

// A script runs its actions without turning the event loop, so we load the plugins its earlier actions added.
std::shared_ptr<engine::Plugin> livePlugin(ProjectController& controller, const model::Insert& insert) {
    syncInstruments(controller);
    const auto plugin = controller.instruments().find(insert.id);
    if (!plugin) {
        throw AppError((insert.plugin ? insert.plugin->name : insert.type) + " of insert " + insert.id.value +
                       " is not loaded, see the track's warning");
    }
    return plugin;
}

ParamValue givenValue(const Json& params) {
    return params.contains("value") && params.at("value").is_string()
               ? ParamValue{params.at("value").get<std::string>()}
               : ParamValue{param<double>(params, "value")};
}

Json setPluginParam(ProjectController& controller, const InsertRef& ref, const std::string& name, const Json& params) {
    const model::Insert& insert = insertOf(controller, ref);
    const auto plugin = livePlugin(controller, insert);
    const auto found = paramNamed(plugin->params(), name);
    if (!found) {
        throw AppError(plugin->name() + " has no single param " + name + ", run insert.params for their names");
    }
    ParamSet set = controller.instruments().setParam(ref.insert, found->id, givenValue(params));
    editTrack(controller, ref.track, "set " + found->name,
              [&](model::Track& edited) { insertIn(edited, ref.insert).plugin->state = std::move(set.state); });
    Json result = insertResult(controller, ref);
    result["param"] = found->name;
    result["value"] = set.value;
    result["text"] = set.text;
    return result;
}

const engine::EffectParam& describedParam(const model::Insert& insert, const std::string& name) {
    const engine::EffectSpec* spec = engine::findEffect(insert.type);
    if (spec == nullptr) {
        throw AppError("insert " + insert.id.value + " is a " + insert.type + " effect, which this build cannot play");
    }
    return effectParamNamed(*spec, name);
}

model::TrackId audioTrack(const Registry& registry, const ProjectController& controller, const Nav& nav,
                          const Json& params) {
    const model::TrackId track = targetTrack(controller, nav, params);
    const model::Track& target = *controller.project().track(track);
    const TrackType* type = registry.trackType(target.kind);
    if (type == nullptr || !playsAudio(*type, target)) {
        throw AppError(playsNoAudio(type, target, "add inserts to an audio track or a bus"));
    }
    return track;
}

Json insertSchema(Json properties, const std::vector<std::string>& required) {
    properties["insert"] = {{"type", "string"}};
    return {{"required", required}, {"properties", std::move(properties)}};
}

void registerInsertEdits(Registry& registry, ProjectController& controller) {
    registry.addAction({.id = "insert.remove",
                        .title = "Remove insert",
                        .aliases = {"delete effect", "remove plugin"},
                        .category = kCategory,
                        .schema = insertSchema(Json::object(), {"insert"}),
                        .run = [&controller](const Json& params) {
                            const InsertRef ref = insertParam(controller, params);
                            return editTrack(controller, ref.track, "remove insert", [&](model::Track& edited) {
                                std::erase_if(edited.inserts,
                                              [&](const model::Insert& insert) { return insert.id == ref.insert; });
                            });
                        }});
    registry.addAction({.id = "insert.move",
                        .title = "Move insert",
                        .aliases = {"reorder effect"},
                        .category = kCategory,
                        .schema = insertSchema({{"index", {{"type", "integer"}}}}, {"insert", "index"}),
                        .run = [&controller](const Json& params) {
                            const InsertRef ref = insertParam(controller, params);
                            const auto index = param<std::int64_t>(params, "index");
                            editTrack(controller, ref.track, "move insert", [&](model::Track& edited) {
                                const auto from = std::ranges::find(edited.inserts, ref.insert, &model::Insert::id);
                                model::Insert moved = std::move(*from);
                                edited.inserts.erase(from);
                                const auto at = std::clamp<std::int64_t>(index, 0, std::ssize(edited.inserts));
                                edited.inserts.insert(edited.inserts.begin() + at, moved);
                            });
                            return insertResult(controller, ref);
                        }});
    registry.addAction({.id = "insert.setBypass",
                        .title = "Bypass insert",
                        .aliases = {"bypass effect", "turn effect off"},
                        .category = kCategory,
                        .schema = insertSchema({{"value", {{"type", "boolean"}}}}, {"insert"}),
                        .run = [&controller](const Json& params) {
                            const InsertRef ref = insertParam(controller, params);
                            editTrack(controller, ref.track, "bypass insert", [&](model::Track& edited) {
                                model::Insert& insert = insertIn(edited, ref.insert);
                                insert.bypass = optionalParam<bool>(params, "value").value_or(!insert.bypass);
                            });
                            return insertResult(controller, ref);
                        }});
    registry.addAction(
        {.id = "insert.setParam",
         .title = "Set insert param",
         .aliases = {"set effect parameter", "change effect"},
         .category = kCategory,
         .schema = insertSchema({{"param", {{"type", "string"}}}, {"value", Json::object()}}, {"param", "value"}),
         .run = [&controller](const Json& params) {
             const InsertRef ref = insertParam(controller, params);
             const std::string name = paramName(params, ref);
             if (insertOf(controller, ref).plugin) {
                 return setPluginParam(controller, ref, name, params);
             }
             const engine::EffectParam& described = describedParam(insertOf(controller, ref), name);
             const double value = effectParamValue(described, params.at("value"));
             editTrack(controller, ref.track, "set " + std::string{described.title},
                       [&](model::Track& edited) { insertIn(edited, ref.insert).params[name] = value; });
             return insertResult(controller, ref);
         }});
    registry.addAction(
        {.id = "insert.params",
         .title = "List insert settings",
         .description = "Lists the params of a plugin insert with their values, the text of each value and the label "
                        "of every choice of a stepped param. insert.setParam takes a param by its name.",
         .aliases = {"effect params", "plugin insert parameters"},
         .category = kCategory,
         .schema = insertSchema(Json::object(), {"insert"}),
         .run =
             [&controller](const Json& params) {
                 const InsertRef ref = insertParam(controller, params);
                 const model::Insert& insert = insertOf(controller, ref);
                 if (!insert.plugin) {
                     return insertResult(controller, ref);
                 }
                 livePlugin(controller, insert);
                 Json described = controller.instruments().describe(ref.insert);
                 described["insert"] = ref.insert.value;
                 described["track"] = ref.track.value;
                 described["message"] = std::to_string(described.at("params").size()) + " settings";
                 return described;
             },
         .notifies = true});
}

void registerInsertEditor(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addAction({.id = "insert.edit",
                        .title = "Open insert editor",
                        .aliases = {"show effect", "edit plugin"},
                        .category = kCategory,
                        .schema = insertSchema(Json::object(), {"insert"}),
                        .run =
                            [&controller, &nav](const Json& params) {
                                const InsertRef ref = insertParam(controller, params);
                                nav.setEditingInsert(qs(ref.insert.value));
                                return insertResult(controller, ref);
                            },
                        .windowOnly = true});
    registry.addAction({.id = "insert.openPluginWindow",
                        .title = "Open plugin window",
                        .description = "Opens the window of a plugin insert's own editor.",
                        .aliases = {"show plugin editor", "plugin gui", "open vst window"},
                        .category = kCategory,
                        .schema = insertSchema(Json::object(), {"insert"}),
                        .run =
                            [&controller](const Json& params) {
                                const InsertRef ref = insertParam(controller, params);
                                const model::Insert& insert = insertOf(controller, ref);
                                if (!insert.plugin) {
                                    throw AppError("insert " + ref.insert.value +
                                                   " is a built-in effect, run insert.edit for its editor");
                                }
                                livePlugin(controller, insert);
                                controller.instruments().showEditor(
                                    ref.insert,
                                    insert.plugin->name + " / " + controller.project().track(ref.track)->name);
                                Json result = insertResult(controller, ref);
                                result["windowOpen"] = controller.instruments().editorOpen(ref.insert);
                                return result;
                            },
                        .windowOnly = true});
    registry.addAction({.id = "insert.closePluginWindow",
                        .title = "Close plugin window",
                        .description = "Closes the window of a plugin insert's own editor.",
                        .aliases = {"hide plugin editor"},
                        .category = kCategory,
                        .schema = insertSchema(Json::object(), {"insert"}),
                        .run = [&controller](const Json& params) {
                            const InsertRef ref = insertParam(controller, params);
                            controller.instruments().closeEditor(ref.insert);
                            Json result = insertResult(controller, ref);
                            result["windowOpen"] = controller.instruments().editorOpen(ref.insert);
                            return result;
                        }});
    registry.addAction({.id = "insert.closeEditor",
                        .title = "Close insert editor",
                        .aliases = {"hide effect"},
                        .category = kCategory,
                        .run = [&nav](const Json&) {
                            nav.setEditingInsert({});
                            return Json::object();
                        }});
}

}  // namespace

void registerInsertActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addAction(
        {.id = "insert.add",
         .title = "Add insert",
         .aliases = {"add effect", "insert plugin", "add eq", "add compressor", "add delay", "add reverb",
                     "add limiter", "add gate", "add filter", "add saturation", "add chorus", "add utility"},
         .category = kCategory,
         .schema = {{"properties",
                     {{"track", {{"type", "string"}}},
                      {"type", {{"type", "string"}, {"description", "a built-in effect"}}},
                      {"plugin", {{"type", "string"}, {"description", "a plugin effect id from plugins.list"}}},
                      {"format", {{"type", "string"}, {"enum", {"clap", "vst3"}}}},
                      {"index", {{"type", "integer"}}}}}},
         .run =
             [&registry, &controller, &nav](const Json& params) {
                 if (params.contains("plugin")) {
                     return registry.run("insert.addPlugin", params);
                 }
                 const model::TrackId track = audioTrack(registry, controller, nav, params);
                 const auto type = param<std::string>(params, "type");
                 const engine::EffectSpec& spec = effectType(type);
                 model::Insert insert{
                     .id = model::InsertId::make(), .type = std::string{spec.id}, .bypass = false, .params = {}};
                 for (const engine::EffectParam& described : spec.params) {
                     insert.params[std::string{described.id}] = described.defaultValue;
                 }
                 const auto index = optionalParam<std::int64_t>(params, "index");
                 editTrack(controller, track, "add " + std::string{spec.title}, [&](model::Track& edited) {
                     const auto at = std::clamp<std::int64_t>(index.value_or(std::ssize(edited.inserts)), 0,
                                                              std::ssize(edited.inserts));
                     edited.inserts.insert(edited.inserts.begin() + at, insert);
                 });
                 if (!registry.fromAgent()) {
                     nav.setEditingInsert(qs(insert.id.value));
                 }
                 return insertResult(controller, {track, insert.id});
             },
         .submenu =
             [] {
                 return engine::kEffects | std::views::transform([](const engine::EffectSpec& spec) {
                            return MenuChoice{.id = std::string{spec.id},
                                              .title = std::string{spec.title},
                                              .params = {{"type", spec.id}}};
                        }) |
                        std::ranges::to<std::vector>();
             }});
    registerInsertEdits(registry, controller);
    registerInsertEditor(registry, controller, nav);
}

void registerPluginInsertActions(Registry& registry, ProjectController& controller, Nav& nav, PluginCatalog& catalog) {
    const auto effects = [&registry, &catalog] {
        return scannedCatalog(registry, catalog).plugins() |
               std::views::filter([](const CatalogPlugin& plugin) { return engine::isEffect(plugin.descriptor); }) |
               std::views::transform([](const CatalogPlugin& plugin) {
                   const std::string format{core::pluginFormatName(plugin.descriptor.format).id};
                   return MenuChoice{.id = format + ":" + plugin.descriptor.id,
                                     .title = plugin.descriptor.name,
                                     .params = {{"plugin", plugin.descriptor.id}, {"format", format}}};
               }) |
               std::ranges::to<std::vector>();
    };
    registry.addAction(
        {.id = "insert.addPlugin",
         .title = "Add plugin insert",
         .description = "Adds a CLAP or VST3 effect from plugins.list as an insert on the audio track or bus. Its "
                        "params then go by name in insert.setParam and in automation.",
         .aliases = {"add vst3 effect", "add plugin effect", "insert vst"},
         .category = kCategory,
         .schema = {{"required", {"plugin"}},
                    {"properties",
                     {{"track", {{"type", "string"}}},
                      {"plugin", {{"type", "string"}, {"description", "a plugin effect id from plugins.list"}}},
                      {"format", {{"type", "string"}, {"enum", {"clap", "vst3"}}}},
                      {"index", {{"type", "integer"}}}}}},
         .run =
             [&registry, &controller, &nav, &catalog](const Json& params) {
                 const model::TrackId track = audioTrack(registry, controller, nav, params);
                 const auto id = param<std::string>(params, "plugin");
                 const auto formatId = optionalParam<std::string>(params, "format");
                 const auto format = formatId ? core::pluginFormatFromId(*formatId) : std::nullopt;
                 if (formatId && !format) {
                     throw AppError("format takes clap or vst3, not " + *formatId);
                 }
                 const auto found = scannedCatalog(registry, catalog).find(id, format);
                 if (!found || !engine::isEffect(found->descriptor)) {
                     throw AppError("no plugin effect " + id + ", run plugins.list for the ids of effects");
                 }
                 const model::Insert insert{.id = model::InsertId::make(),
                                            .type = model::kPluginInsertType,
                                            .bypass = false,
                                            .params = {},
                                            .plugin = model::PluginSlot{.pluginId = id,
                                                                        .bundle = found->bundle.string(),
                                                                        .name = found->descriptor.name,
                                                                        .state = {},
                                                                        .format = found->descriptor.format,
                                                                        .vendor = found->descriptor.vendor}};
                 const auto index = optionalParam<std::int64_t>(params, "index");
                 editTrack(controller, track, "add " + found->descriptor.name, [&](model::Track& edited) {
                     const auto at = std::clamp<std::int64_t>(index.value_or(std::ssize(edited.inserts)), 0,
                                                              std::ssize(edited.inserts));
                     edited.inserts.insert(edited.inserts.begin() + at, insert);
                 });
                 controller.instruments().sync(controller.project().document());
                 if (!registry.fromAgent()) {
                     nav.setEditingInsert(qs(insert.id.value));
                 }
                 return insertResult(controller, {track, insert.id});
             },
         .submenu = effects});
}

}  // namespace emerick::app
