#include <algorithm>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <variant>

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

}  // namespace

void registerClipActions(Registry& registry, ProjectController& controller) {
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
    registry.addAction({.id = "clip.resize",
                        .title = "Resize clip",
                        .aliases = {"trim clip"},
                        .category = "Clips",
                        .schema = {{"required", {"clip"}},
                                   {"properties", {{"start", Json::object()}, {"length", {{"type", "integer"}}}}}},
                        .run = [&controller, tempo](const Json& params) {
                            const auto id = clipParam(controller.project(), params);
                            const auto start = optionalPosition(params, "start", tempo());
                            const auto length = optionalParam<core::Ticks>(params, "length");
                            if (!start && !length) {
                                throw AppError("pass start, length or both");
                            }
                            model::Clip clip = clipOf(controller, id);
                            if (start) {
                                clip = edited(model::trimStart(clip, *start, tempo(), controller.sampleRate()));
                            }
                            if (length) {
                                clip = edited(model::setLength(clip, *length));
                            }
                            replaceClip(controller, id, "resize clip", std::move(clip));
                            return clipResult(controller, id);
                        }});
    registry.addAction(
        {.id = "clip.split",
         .title = "Split clip",
         .aliases = {"cut clip"},
         .category = "Clips",
         .schema = {{"required", {"clip", "at"}}},
         .run = [&controller, tempo](const Json& params) {
             const auto id = clipParam(controller.project(), params);
             const core::Ticks at = positionParam(params, "at", tempo());
             auto [left, right] = edited(model::split(clipOf(controller, id), at, tempo(), controller.sampleRate()));
             const model::ClipId rightId = model::clipId(right);
             replaceClip(controller, id, "split clip", std::move(left), {std::move(right)});
             return Json{{"left", clipResult(controller, id)}, {"right", clipResult(controller, rightId)}};
         }});
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
                        .aliases = {"remove clip"},
                        .category = "Clips",
                        .schema = {{"required", {"clip"}}},
                        .run = [&controller](const Json& params) {
                            const auto id = clipParam(controller.project(), params);
                            const model::TrackId track = clipRef(controller, id).track->id;
                            controller.project().editTrack(track, "delete clip", [&](model::Track& edited) {
                                std::erase_if(edited.clips,
                                              [&](const model::Clip& c) { return model::clipId(c) == id; });
                            });
                            return projectState(controller);
                        }});
    registry.addAction({.id = "clip.duplicate",
                        .title = "Duplicate clip",
                        .aliases = {"copy clip"},
                        .category = "Clips",
                        .schema = {{"required", {"clip"}}, {"properties", {{"at", Json::object()}}}},
                        .run = [&controller, tempo](const Json& params) {
                            const auto id = clipParam(controller.project(), params);
                            const model::Clip& clip = clipOf(controller, id);
                            model::Clip copy = model::duplicateClip(
                                clip, optionalPosition(params, "at", tempo()).value_or(model::clipEnd(clip)));
                            const model::ClipId copyId = model::clipId(copy);
                            replaceClip(controller, id, "duplicate clip", clip, {std::move(copy)});
                            return clipResult(controller, copyId);
                        }});
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
}

}  // namespace emerick::app
