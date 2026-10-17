#include <QStringList>
#include <algorithm>
#include <expected>
#include <filesystem>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "action_params.hpp"
#include "emerick/app/audio_file.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/core/overloaded.hpp"
#include "emerick/model/clip_edit.hpp"

namespace emerick::app {

namespace {

Json clipResult(const ProjectController& controller, const model::ClipId& id) {
    const auto found = controller.project().findClip(id);
    return found ? clipState(*found->clip, controller.project().document().tempo) : Json::object();
}

Json clipResults(const ProjectController& controller, const std::vector<model::ClipId>& ids) {
    return Json{{"clips", ids | std::views::transform([&](const model::ClipId& id) {
                              return clipResult(controller, id);
                          }) | std::ranges::to<Json::array_t>()}};
}

template <class T>
T edited(std::expected<T, std::string> result) {
    if (!result) {
        throw AppError(result.error());
    }
    return std::move(*result);
}

model::ClipRef clipRef(const ProjectController& controller, const model::ClipId& id) {
    const auto found = controller.project().findClip(id);
    if (!found) {
        throw AppError("no clip " + id.value);
    }
    return *found;
}

const model::Clip& clipOf(const ProjectController& controller, const model::ClipId& id) {
    return *clipRef(controller, id).clip;
}

// Replaces the clip with id by `replacement`, then adds `added` to the same track, as one undo step.
void replaceClip(ProjectController& controller, const model::ClipId& id, std::string name, model::Clip replacement,
                 std::vector<model::Clip> added = {}) {
    controller.project().editTrack(clipRef(controller, id).track->id, std::move(name), [&](model::Track& edit) {
        *std::ranges::find(edit.clips, id, [](const model::Clip& c) { return model::clipId(c); }) =
            std::move(replacement);
        std::ranges::move(added, std::back_inserter(edit.clips));
    });
}

model::AudioClip audioOnly(const model::Clip& clip) {
    return std::visit(
        core::Overloaded{[](const model::AudioClip& audio) { return audio; },
                         [](const model::MidiClip& midi) -> model::AudioClip {
                             throw AppError("clip " + midi.id.value + " is a MIDI clip, which has no fades or gain");
                         }},
        clip);
}

std::vector<model::ClipId> selection(const ProjectController& controller, const Nav& nav) {
    return nav.selectedClips() |
           std::views::transform([](const QString& id) { return model::ClipId{id.toStdString()}; }) |
           std::views::filter([&](const model::ClipId& id) { return controller.project().findClip(id).has_value(); }) |
           std::ranges::to<std::vector>();
}

// The clip named by params, or the selected clips when params names none.
std::vector<model::ClipId> targetClips(const ProjectController& controller, const Nav& nav, const Json& params) {
    if (params.contains("clip")) {
        return {clipParam(controller.project(), params)};
    }
    auto selected = selection(controller, nav);
    if (selected.empty()) {
        throw AppError("select a clip first, or pass clip");
    }
    return selected;
}

// Selecting clips also selects the first clip's track, so the inspector shows both.
void select(const ProjectController& controller, Nav& nav, const std::vector<model::ClipId>& ids) {
    nav.setSelectedClips(ids | std::views::transform([](const model::ClipId& id) { return qs(id.value); }) |
                         std::ranges::to<QStringList>());
    if (!ids.empty()) {
        nav.setSelectedTrack(qs(clipRef(controller, ids.front()).track->id.value));
    }
}

Json importClip(const Registry& registry, ProjectController& controller, const Json& params) {
    const model::TrackId id = trackParam(controller.project(), params);
    const model::Track& track = *controller.project().track(id);
    const TrackType* type = registry.trackType(track.kind);
    if (type == nullptr || !type->routes.audio) {
        throw AppError("track " + track.name + " is not an audio track");
    }
    const std::filesystem::path source{param<std::string>(params, "path")};
    const core::TempoMap& tempo = controller.project().document().tempo;
    model::AudioClip clip = clipFromFile(source, controller.folder(), optionalPosition(params, "at", tempo).value_or(0),
                                         tempo, controller.sampleRate());
    const model::ClipId clipId = clip.id;
    controller.project().editTrack(id, "import " + source.filename().string(),
                                   [&](model::Track& edited) { edited.clips.emplace_back(std::move(clip)); });
    return clipResult(controller, clipId);
}

Json splitClips(ProjectController& controller, const Nav& nav, const Json& params) {
    const core::TempoMap& tempo = controller.project().document().tempo;
    const core::Ticks at = optionalPosition(params, "at", tempo).value_or(controller.positionTicks());
    const auto splitOne = [&](const model::ClipId& id) {
        auto [left, right] = edited(model::split(clipOf(controller, id), at, tempo, controller.sampleRate()));
        const model::ClipId rightId = model::clipId(right);
        replaceClip(controller, id, "split clip", std::move(left), {std::move(right)});
        return Json{{"left", clipResult(controller, id)}, {"right", clipResult(controller, rightId)}};
    };
    if (params.contains("clip")) {
        return splitOne(clipParam(controller.project(), params));
    }
    const auto inside = [&](const model::ClipId& id) {
        const model::Clip& clip = clipOf(controller, id);
        return at > model::clipStart(clip) && at < model::clipEnd(clip);
    };
    const auto ids = targetClips(controller, nav, params) | std::views::filter(inside) | std::ranges::to<std::vector>();
    if (ids.empty()) {
        throw AppError("the split position is not inside a selected clip");
    }
    return Json{{"splits", ids | std::views::transform(splitOne) | std::ranges::to<Json::array_t>()}};
}

Json duplicateClips(ProjectController& controller, Nav& nav, const Json& params) {
    const auto at = optionalPosition(params, "at", controller.project().document().tempo);
    const auto duplicateOne = [&](const model::ClipId& id) {
        const model::Clip& clip = clipOf(controller, id);
        model::Clip copy = model::duplicateClip(clip, at.value_or(model::clipEnd(clip)));
        model::ClipId copyId = model::clipId(copy);
        replaceClip(controller, id, "duplicate clip", clip, {std::move(copy)});
        return copyId;
    };
    if (params.contains("clip")) {
        return clipResult(controller, duplicateOne(clipParam(controller.project(), params)));
    }
    const auto copies =
        targetClips(controller, nav, params) | std::views::transform(duplicateOne) | std::ranges::to<std::vector>();
    select(controller, nav, copies);
    return clipResults(controller, copies);
}

void registerSelectionActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addAction({.id = "selection.set",
                        .title = "Select clips",
                        .aliases = {"select clip"},
                        .category = "Clips",
                        .schema = {{"required", {"clips"}}, {"properties", {{"clips", {{"type", "array"}}}}}},
                        .run = [&controller, &nav](const Json& params) {
                            const auto ids = param<std::vector<std::string>>(params, "clips") |
                                             std::views::transform([&](const std::string& id) {
                                                 return clipParam(controller.project(), Json{{"clip", id}});
                                             }) |
                                             std::ranges::to<std::vector>();
                            select(controller, nav, ids);
                            return nav.toJson();
                        }});
    registry.addAction({.id = "selection.clear",
                        .title = "Deselect all clips",
                        .aliases = {"clear selection", "select none"},
                        .category = "Clips",
                        .run = [&nav](const Json&) {
                            nav.setSelectedClips({});
                            return nav.toJson();
                        }});
    registry.addSetting({.id = "edit.snap",
                         .title = "Snap to beat grid",
                         .aliases = {"snap", "grid"},
                         .category = "Settings / Editing",
                         .type = SettingType::Boolean,
                         .defaultValue = true,
                         .hint = "Hold Alt while dragging to place freely",
                         .location = "settings.general"});
    registry.addAction(
        {.id = "edit.toggleSnap",
         .title = "Toggle snap to grid",
         .aliases = {"snap on", "snap off", "grid snap"},
         .category = "Clips",
         .shortcut = "N",
         .run = [&registry](const Json&) {
             return Json{{"snap", registry.setSetting("edit.snap", !registry.settingValue("edit.snap").get<bool>())}};
         }});
    // Undo, delete and track removal can take selected clips away, so we drop their ids from the selection.
    controller.project().addListener([&controller, &nav](const model::Change&) {
        const auto kept = selection(controller, nav);
        if (std::cmp_not_equal(kept.size(), nav.selectedClips().size())) {
            nav.setSelectedClips(kept | std::views::transform([](const model::ClipId& id) { return qs(id.value); }) |
                                 std::ranges::to<QStringList>());
        }
    });
}

}  // namespace

void registerClipActions(Registry& registry, ProjectController& controller, Nav& nav) {
    const auto tempo = [&controller]() -> const core::TempoMap& { return controller.project().document().tempo; };

    registry.addAction({.id = "clip.move",
                        .title = "Move clip",
                        .category = "Clips",
                        .schema = {{"required", {"clip", "start"}}},
                        .run = [&controller, tempo](const Json& params) {
                            const auto id = clipParam(controller.project(), params);
                            const core::Ticks start = positionParam(params, "start", tempo());
                            if (start < 0) {
                                throw AppError("start must be at or after bar 1");
                            }
                            replaceClip(controller, id, "move clip", model::moveClip(clipOf(controller, id), start));
                            return clipResult(controller, id);
                        }});
    registry.addAction(
        {.id = "clip.resize",
         .title = "Resize clip",
         .aliases = {"trim clip"},
         .category = "Clips",
         .schema = {{"required", {"clip"}},
                    {"properties",
                     {{"start", Json::object()}, {"length", {{"type", "integer"}}}, {"end", Json::object()}}}},
         .run = [&controller, tempo](const Json& params) {
             const auto id = clipParam(controller.project(), params);
             const auto start = optionalPosition(params, "start", tempo());
             const auto end = optionalPosition(params, "end", tempo());
             auto length = optionalParam<core::Ticks>(params, "length");
             if (!start && !length && !end) {
                 throw AppError("pass start, length, end or a mix");
             }
             model::Clip clip = clipOf(controller, id);
             if (start) {
                 clip = edited(model::trimStart(clip, *start, tempo(), controller.sampleRate()));
             }
             if (end) {
                 length = *end - model::clipStart(clip);
             }
             if (length) {
                 clip = edited(model::setLength(clip, *length));
             }
             replaceClip(controller, id, "resize clip", std::move(clip));
             return clipResult(controller, id);
         }});
    registry.addAction(
        {.id = "clip.split",
         .title = "Split clip at playhead",
         .aliases = {"cut clip", "split region"},
         .category = "Clips",
         .shortcut = "Ctrl+T",
         .schema = {{"properties", {{"clip", {{"type", "string"}}}, {"at", Json::object()}}}},
         .run = [&controller, &nav](const Json& params) { return splitClips(controller, nav, params); }});
    registry.addAction({.id = "clip.setFade",
                        .title = "Set clip fades",
                        .aliases = {"fade in", "fade out"},
                        .category = "Clips",
                        .schema = {{"required", {"clip"}}},
                        .run = [&controller](const Json& params) {
                            const auto id = clipParam(controller.project(), params);
                            const auto fadeIn = optionalParam<core::Ticks>(params, "fadeIn");
                            const auto fadeOut = optionalParam<core::Ticks>(params, "fadeOut");
                            model::AudioClip audio = audioOnly(clipOf(controller, id));
                            audio.fadeIn = std::clamp<core::Ticks>(fadeIn.value_or(audio.fadeIn), 0, audio.length);
                            audio.fadeOut = std::clamp<core::Ticks>(fadeOut.value_or(audio.fadeOut), 0, audio.length);
                            replaceClip(controller, id, "set clip fades", std::move(audio));
                            return clipResult(controller, id);
                        }});
    registry.addAction({.id = "clip.setGain",
                        .title = "Set clip gain",
                        .aliases = {"clip volume"},
                        .category = "Clips",
                        .schema = {{"required", {"clip", "db"}}},
                        .run = [&controller](const Json& params) {
                            const auto id = clipParam(controller.project(), params);
                            model::AudioClip audio = audioOnly(clipOf(controller, id));
                            audio.gainDb = param<double>(params, "db");
                            replaceClip(controller, id, "set clip gain", std::move(audio));
                            return clipResult(controller, id);
                        }});
    registry.addAction({.id = "clip.delete",
                        .title = "Delete clip",
                        .aliases = {"remove clip", "delete selected clips"},
                        .category = "Clips",
                        .shortcut = "Backspace",
                        .schema = {{"properties", {{"clip", {{"type", "string"}}}}}},
                        .run = [&controller, &nav](const Json& params) {
                            for (const model::ClipId& id : targetClips(controller, nav, params)) {
                                const model::TrackId track = clipRef(controller, id).track->id;
                                controller.project().editTrack(track, "delete clip", [&](model::Track& edited) {
                                    std::erase_if(edited.clips,
                                                  [&](const model::Clip& c) { return model::clipId(c) == id; });
                                });
                            }
                            return projectState(controller);
                        }});
    registry.addAction(
        {.id = "clip.duplicate",
         .title = "Duplicate clip",
         .aliases = {"copy clip", "repeat clip"},
         .category = "Clips",
         .shortcut = "Ctrl+D",
         .schema = {{"properties", {{"clip", {{"type", "string"}}}, {"at", Json::object()}}}},
         .run = [&controller, &nav](const Json& params) { return duplicateClips(controller, nav, params); }});
    registry.addAction(
        {.id = "clip.import",
         .title = "Import audio file",
         .aliases = {"add audio file", "import clip"},
         .category = "Clips",
         .schema = {{"required", {"track", "path"}}, {"properties", {{"at", Json::object()}}}},
         .run = [&registry, &controller](const Json& params) { return importClip(registry, controller, params); }});
    registry.addAction({.id = "project.clipEvents",
                        .title = "Show clip MIDI events",
                        .category = "Clips",
                        .schema = {{"required", {"clip"}}, {"properties", {{"includeEvents", {{"type", "boolean"}}}}}},
                        .run = [&controller, tempo](const Json& params) {
                            const auto id = clipParam(controller.project(), params);
                            return clipState(clipOf(controller, id), tempo(),
                                             optionalParam<bool>(params, "includeEvents").value_or(true));
                        }});
    registerSelectionActions(registry, controller, nav);
}

}  // namespace emerick::app
