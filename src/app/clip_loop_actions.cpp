#include <algorithm>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/stem_export.hpp"
#include "emerick/core/overloaded.hpp"
#include "emerick/model/clip_edit.hpp"
#include "emerick/model/midi_notes.hpp"

namespace emerick::app {

namespace {

model::MidiClip midiOnly(const model::Clip& clip) {
    return std::visit(
        core::Overloaded{[](const model::AudioClip& audio) -> model::MidiClip {
                             throw AppError("clip " + audio.id.value + " is an audio clip, and only MIDI clips loop");
                         },
                         [](const model::MidiClip& midi) { return midi; }},
        clip);
}

model::ClipRef existing(const ProjectController& controller, const model::ClipId& id) {
    const auto found = controller.project().findClip(id);
    if (!found) {
        throw AppError("no clip " + id.value);
    }
    return *found;
}

// Where a clip looped from the toggle stops: at the next clip on its track, or else at the end of the project.
core::Ticks loopEnd(const model::Document& document, const model::Track& track, const model::MidiClip& clip) {
    const core::Ticks contentEnd = clip.start + clip.length;
    auto later = track.clips | std::views::transform(model::clipStart) |
                 std::views::filter([&](core::Ticks start) { return start >= contentEnd; });
    return later.empty() ? projectEnd(document) : std::ranges::min(later);
}

model::MidiClip looping(const model::Document& document, const model::Track& track, model::MidiClip clip, bool value) {
    if (!value) {
        clip.loopLength = 0;
        return clip;
    }
    if (model::looped(clip)) {
        return clip;
    }
    const core::Ticks end = loopEnd(document, track, clip);
    if (end <= clip.start + clip.length) {
        throw AppError("clip " + clip.id.value +
                       " reaches the next clip or the project end already, so drag its loop handle to loop it");
    }
    clip.loopLength = end - clip.start;
    return clip;
}

void replaceMidi(ProjectController& controller, const model::ClipId& id, const std::string& name,
                 const model::Clip& clip) {
    controller.project().editTrack(existing(controller, id).track->id, name, [&](model::Track& track) {
        *std::ranges::find(track.clips, id, model::clipId) = clip;
    });
}

// Without value, loops the clips unless every one of them loops already.
Json loopClips(ProjectController& controller, const Nav& nav, const Json& params) {
    const auto ids = targetClips(controller, nav, params);
    const bool value =
        optionalParam<bool>(params, "value").value_or(!std::ranges::all_of(ids, [&](const model::ClipId& id) {
            return model::looped(midiOnly(*existing(controller, id).clip));
        }));
    const std::string name = value ? "loop clip" : "unloop clip";
    controller.project().group(name, [&] {
        for (const model::ClipId& id : ids) {
            const model::ClipRef ref = existing(controller, id);
            replaceMidi(controller, id, name,
                        looping(controller.project().document(), *ref.track, midiOnly(*ref.clip), value));
        }
    });
    return clipResults(controller, ids);
}

Json setLoopLength(ProjectController& controller, const Json& params) {
    const auto id = clipParam(controller.project(), params);
    const model::Clip& clip = *existing(controller, id).clip;
    const core::TempoMap& tempo = controller.project().document().tempo;
    const auto end = optionalPosition(params, "end", tempo);
    const auto length =
        end ? std::optional{*end - model::clipStart(clip)} : optionalParam<core::Ticks>(params, "length");
    if (!length) {
        throw AppError("pass length or end");
    }
    auto looped = model::setLoopLength(clip, *length);
    if (!looped) {
        throw AppError(looped.error());
    }
    replaceMidi(controller, id, "set loop length", *looped);
    return clipResult(controller, id);
}

}  // namespace

void registerClipLoopActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addAction(
        {.id = "clip.loop",
         .title = "Loop clip",
         .description = "Loops the MIDI clips until the next clip on their track or the end of the project, or ends "
                        "their loops when every one loops already. Editing the content changes every repeat.",
         .aliases = {"loop region", "repeat region", "unloop clip"},
         .category = "Clips",
         .shortcut = "L",
         .schema = {{"properties", {{"clip", {{"type", "string"}}}, {"value", {{"type", "boolean"}}}}}},
         .run = [&controller, &nav](const Json& params) { return loopClips(controller, nav, params); },
         .menus = {kEditMenu, kClipContextMenu}});
    registry.addAction({.id = "clip.setLoopLength",
                        .title = "Set loop length",
                        .description = "Loops the MIDI clip's content for length ticks on the timeline, or until end, "
                                       "with a partial last repeat where it falls short. A length up to the content's "
                                       "own ends the loop.",
                        .aliases = {"extend loop", "drag loop"},
                        .category = "Clips",
                        .schema = {{"required", {"clip"}},
                                   {"properties", {{"length", {{"type", "integer"}}}, {"end", Json::object()}}}},
                        .run = [&controller](const Json& params) { return setLoopLength(controller, params); }});
}

}  // namespace emerick::app
