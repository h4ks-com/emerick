#include <algorithm>
#include <cmath>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/engine/effects.hpp"

namespace emerick::app {

namespace {

constexpr const char* kCategory = "Tracks / Inserts";

const engine::EffectSpec& effectParam(const Json& params) {
    const auto type = param<std::string>(params, "type");
    const engine::EffectSpec* spec = engine::findEffect(type);
    if (spec == nullptr) {
        std::string known;
        for (const engine::EffectSpec& effect : engine::kEffects) {
            known += (known.empty() ? "" : ", ") + std::string{effect.id};
        }
        throw AppError("unknown effect type " + type + ", use one of " + known);
    }
    return *spec;
}

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

std::string paramName(const Json& params) {
    const auto name = param<std::string>(params, "param");
    return name.contains('.') ? name.substr(name.find('.') + 1) : name;
}

model::Insert& insertIn(model::Track& track, const model::InsertId& id) {
    return *std::ranges::find(track.inserts, id, &model::Insert::id);
}

const model::Insert& insertOf(const ProjectController& controller, const InsertRef& ref) {
    const model::Track& track = *controller.project().track(ref.track);
    return *std::ranges::find(track.inserts, ref.insert, &model::Insert::id);
}

Json insertResult(const ProjectController& controller, const InsertRef& ref) {
    Json result = insertState(insertOf(controller, ref));
    result["track"] = ref.track.value;
    return result;
}

// Toggles store 0 or 1 and choices a whole index; a choice also accepts its name, such as "1/8".
double paramValue(const engine::EffectParam& described, const Json& params) {
    const Json& given = params.at("value");
    double value = 0.0;
    if (given.is_string() && described.kind == engine::ParamKind::choice) {
        const auto found = std::ranges::find(described.choices, given.get<std::string>());
        if (found == described.choices.end()) {
            throw AppError("param " + std::string{described.id} + " has no choice " + given.get<std::string>());
        }
        value = static_cast<double>(found - described.choices.begin());
    } else {
        value = param<double>(params, "value");
    }
    switch (described.kind) {
        case engine::ParamKind::toggle:
            value = value != 0.0 ? 1.0 : 0.0;
            break;
        case engine::ParamKind::choice:
            value = std::round(value);
            break;
        case engine::ParamKind::number:
            break;
    }
    return std::clamp(value, static_cast<double>(described.minimum), static_cast<double>(described.maximum));
}

const engine::EffectParam& describedParam(const model::Insert& insert, const std::string& name) {
    const engine::EffectSpec* spec = engine::findEffect(insert.type);
    if (spec == nullptr) {
        throw AppError("insert " + insert.id.value + " is a " + insert.type + " effect, which this build cannot play");
    }
    const auto index = engine::paramIndex(*spec, name);
    if (!index) {
        std::string known;
        for (const engine::EffectParam& described : spec->params) {
            known += (known.empty() ? "" : ", ") + std::string{described.id};
        }
        throw AppError(std::string{spec->title} + " has no param " + name + ", use one of " + known);
    }
    return spec->params[*index];
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
             const std::string name = paramName(params);
             const engine::EffectParam& described = describedParam(insertOf(controller, ref), name);
             const double value = paramValue(described, params);
             editTrack(controller, ref.track, "set " + std::string{described.title},
                       [&](model::Track& edited) { insertIn(edited, ref.insert).params[name] = value; });
             return insertResult(controller, ref);
         }});
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
    registry.addAction({.id = "insert.closeEditor",
                        .title = "Close insert editor",
                        .aliases = {"hide effect"},
                        .category = kCategory,
                        .enabled = [&nav] { return !nav.editingInsert().isEmpty(); },
                        .run =
                            [&nav](const Json&) {
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
                     "add limiter"},
         .category = kCategory,
         .schema =
             {{"required", {"type"}},
              {"properties",
               {{"track", {{"type", "string"}}}, {"type", {{"type", "string"}}}, {"index", {{"type", "integer"}}}}}},
         .run =
             [&registry, &controller, &nav](const Json& params) {
                 const model::TrackId track = audioTrack(registry, controller, nav, params);
                 const engine::EffectSpec& spec = effectParam(params);
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
                 nav.setEditingInsert(qs(insert.id.value));
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

}  // namespace emerick::app
