#include <algorithm>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/core/fade.hpp"
#include "emerick/core/overloaded.hpp"
#include "emerick/model/clip_edit.hpp"

namespace emerick::app {

namespace {

std::string shapeIds() {
    std::string ids;
    for (const core::FadeShapeName& name : core::kFadeShapes) {
        ids += (ids.empty() ? "" : ", ") + std::string{name.id};
    }
    return ids;
}

std::optional<core::FadeShape> optionalShape(const Json& params, const std::string& key) {
    const auto id = optionalParam<std::string>(params, key);
    if (!id) {
        return std::nullopt;
    }
    const auto shape = core::fadeShapeFromId(*id);
    if (!shape) {
        throw AppError("param " + key + " is not a fade shape: " + *id + "; use one of " + shapeIds());
    }
    return shape;
}

Json shapeSchema() {
    return {{"type", "string"}, {"enum", core::kFadeShapes | std::views::transform([](const core::FadeShapeName& name) {
                                             return std::string{name.id};
                                         }) | std::ranges::to<std::vector>()}};
}

std::vector<MenuChoice> shapeChoices() {
    return core::kFadeShapes | std::views::transform([](const core::FadeShapeName& name) {
               return MenuChoice{.id = std::string{name.id},
                                 .title = std::string{name.title},
                                 .params = {{"shape", std::string{name.id}}}};
           }) |
           std::ranges::to<std::vector>();
}

model::TrackId trackOf(const ProjectController& controller, const model::ClipId& id) {
    const auto found = controller.project().findClip(id);
    if (!found) {
        throw AppError("no clip " + id.value);
    }
    return found->track->id;
}

// Edits every audio clip among ids as one undo step; a MIDI clip has no fades, so an explicit one is an error and a
// selected one is skipped.
void editFades(ProjectController& controller, const std::vector<model::ClipId>& ids, bool explicitClip,
               const std::string& name, const std::function<void(model::AudioClip&)>& edit) {
    controller.project().group(name, [&] {
        for (const model::ClipId& id : ids) {
            controller.project().editTrack(trackOf(controller, id), name, [&](model::Track& edited) {
                model::Clip& clip =
                    *std::ranges::find(edited.clips, id, [](const model::Clip& c) { return model::clipId(c); });
                std::visit(core::Overloaded{edit,
                                            [&](model::MidiClip& midi) {
                                                if (explicitClip) {
                                                    throw AppError("clip " + midi.id.value +
                                                                   " is a MIDI clip, which has no fades");
                                                }
                                            }},
                           clip);
            });
        }
    });
}

void setFade(model::AudioClip& audio, const Json& params) {
    audio.fadeIn =
        std::clamp<core::Ticks>(optionalParam<core::Ticks>(params, "fadeIn").value_or(audio.fadeIn), 0, audio.length);
    audio.fadeOut =
        std::clamp<core::Ticks>(optionalParam<core::Ticks>(params, "fadeOut").value_or(audio.fadeOut), 0, audio.length);
    audio.fadeInShape = optionalShape(params, "fadeInShape").value_or(audio.fadeInShape);
    audio.fadeOutShape = optionalShape(params, "fadeOutShape").value_or(audio.fadeOutShape);
    audio.fadeInCurve = std::clamp(optionalParam<double>(params, "fadeInCurve").value_or(audio.fadeInCurve), -1.0, 1.0);
    audio.fadeOutCurve =
        std::clamp(optionalParam<double>(params, "fadeOutCurve").value_or(audio.fadeOutCurve), -1.0, 1.0);
}

void crossfade(ProjectController& controller, const model::ClipId& id) {
    model::Track track = *controller.project().track(trackOf(controller, id));
    if (const auto done = model::crossfade(track, id); !done) {
        throw AppError(done.error());
    }
    controller.project().editTrack(track.id, "create crossfade",
                                   [&](model::Track& edited) { edited.clips = std::move(track.clips); });
}

}  // namespace

void registerFadeActions(Registry& registry, ProjectController& controller, Nav& nav) {
    const Json curveSchema = {{"type", "number"}, {"minimum", -1}, {"maximum", 1}};
    registry.addAction(
        {.id = "clip.setFade",
         .title = "Set clip fades",
         .description = "Sets fade lengths in ticks, shapes, and curves from -1 to 1 that lower or raise the fade's "
                        "middle.",
         .aliases = {"fade in", "fade out", "fade curve"},
         .category = "Clips",
         .schema = {{"required", {"clip"}},
                    {"properties",
                     {{"clip", {{"type", "string"}}},
                      {"fadeIn", {{"type", "integer"}}},
                      {"fadeOut", {{"type", "integer"}}},
                      {"fadeInShape", shapeSchema()},
                      {"fadeOutShape", shapeSchema()},
                      {"fadeInCurve", curveSchema},
                      {"fadeOutCurve", curveSchema}}}},
         .run = [&controller](const Json& params) {
             const model::ClipId id = clipParam(controller.project(), params);
             editFades(controller, {id}, true, "set clip fades",
                       [&](model::AudioClip& audio) { setFade(audio, params); });
             return clipResult(controller, id);
         }});
    const auto shapeAction = [&](const char* id, const char* title, const char* key, const char* alias) {
        registry.addAction({.id = id,
                            .title = title,
                            .aliases = {alias},
                            .category = "Clips",
                            .schema = {{"required", {"shape"}},
                                       {"properties", {{"clip", {{"type", "string"}}}, {"shape", shapeSchema()}}}},
                            .run =
                                [&controller, &nav, key](const Json& params) {
                                    const Json fade = {{key, param<std::string>(params, "shape")}};
                                    const auto ids = targetClips(controller, nav, params);
                                    editFades(controller, ids, params.contains("clip"), "set fade shape",
                                              [&](model::AudioClip& audio) { setFade(audio, fade); });
                                    return clipResults(controller, ids);
                                },
                            .menus = {kClipContextMenu},
                            .submenu = shapeChoices});
    };
    shapeAction("clip.setFadeInShape", "Set fade in shape", "fadeInShape", "fade in curve");
    shapeAction("clip.setFadeOutShape", "Set fade out shape", "fadeOutShape", "fade out curve");
    registry.addAction({.id = "clip.crossfade",
                        .title = "Create crossfade",
                        .description = "Crossfades the clip with the clip that overlaps its start or its end on the "
                                       "same track, with equal power fades over the overlap.",
                        .aliases = {"x-fade", "crossfade clips"},
                        .category = "Clips",
                        .schema = {{"properties", {{"clip", {{"type", "string"}}}}}},
                        .run =
                            [&controller, &nav](const Json& params) {
                                const auto ids = targetClips(controller, nav, params);
                                controller.project().group("create crossfade", [&] {
                                    for (const model::ClipId& id : ids) {
                                        crossfade(controller, id);
                                    }
                                });
                                return clipResults(controller, ids);
                            },
                        .menus = {kClipContextMenu}});
    registry.addAction({.id = "clip.resetFades",
                        .title = "Reset clip fades",
                        .aliases = {"remove fades", "clear fade in", "clear fade out"},
                        .category = "Clips",
                        .schema = {{"properties", {{"clip", {{"type", "string"}}}}}},
                        .run =
                            [&controller, &nav](const Json& params) {
                                const auto ids = targetClips(controller, nav, params);
                                editFades(controller, ids, false, "reset clip fades", [](model::AudioClip& audio) {
                                    audio.fadeIn = 0;
                                    audio.fadeOut = 0;
                                });
                                return clipResults(controller, ids);
                            },
                        .menus = {kClipContextMenu}});
}

}  // namespace emerick::app
