#include "emerick/app/note_edit.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/core/overloaded.hpp"

namespace emerick::app {

core::Ticks gridTicks(const Registry& registry) {
    constexpr core::Ticks kWholeNote = 4 * core::kTicksPerQuarter;
    const int division = registry.setting(kGridSetting) == nullptr ? 0 : registry.settingValue(kGridSetting).get<int>();
    return division > 0 ? kWholeNote / division : core::kTicksPerQuarter / 4;
}

model::ClipId targetClip(const ProjectController& controller, const Nav& nav, const Json& params) {
    if (params.contains("clip")) {
        return clipParam(controller.project(), params);
    }
    if (nav.pianoRollClip().isEmpty()) {
        throw AppError("open a MIDI clip in the piano roll first, or pass clip");
    }
    return clipParam(controller.project(), Json{{"clip", nav.pianoRollClip().toStdString()}});
}

const model::MidiClip& midiClipOf(const ProjectController& controller, const model::ClipId& id) {
    const auto found = controller.project().findClip(id);
    if (!found) {
        throw AppError("no clip " + id.value);
    }
    return *std::visit(
        core::Overloaded{[](const model::AudioClip& audio) -> const model::MidiClip* {
                             throw AppError("clip " + audio.id.value + " is an audio clip, which has no notes");
                         },
                         [](const model::MidiClip& midi) { return &midi; }},
        *found->clip);
}

std::vector<model::NoteId> targetNotes(const ProjectController& controller, const Nav& nav, const model::ClipId& clip,
                                       const Json& params, AllWhenNone all) {
    const model::MidiClip& midi = midiClipOf(controller, clip);
    const auto exists = [&](const model::NoteId& id) {
        return std::ranges::contains(midi.notes, id, &model::Note::id);
    };
    if (params.contains("notes")) {
        auto ids = param<std::vector<std::string>>(params, "notes") |
                   std::views::transform([](const std::string& id) { return model::NoteId{id}; }) |
                   std::ranges::to<std::vector>();
        if (const auto missing = std::ranges::find_if_not(ids, exists); missing != ids.end()) {
            throw AppError("no note " + missing->value + " in clip " + clip.value +
                           ", read project.clipEvents for note ids");
        }
        return ids;
    }
    auto selected = nav.pianoRollClip() == qs(clip.value)
                        ? nav.selectedNotes() |
                              std::views::transform([](const QString& id) { return model::NoteId{id.toStdString()}; }) |
                              std::views::filter(exists) | std::ranges::to<std::vector>()
                        : std::vector<model::NoteId>{};
    if (selected.empty() && all == AllWhenNone::yes) {
        return midi.notes | std::views::transform(&model::Note::id) | std::ranges::to<std::vector>();
    }
    if (selected.empty()) {
        throw AppError("select notes first, or pass notes");
    }
    return selected;
}

void editNotes(ProjectController& controller, const model::ClipId& clip, const std::string& name,
               const std::function<void(model::MidiClip&)>& edit) {
    const auto found = controller.project().findClip(clip);
    if (!found) {
        throw AppError("no clip " + clip.value);
    }
    controller.project().editTrack(found->track->id, name, [&](model::Track& edited) {
        model::Clip& target = *std::ranges::find(edited.clips, clip,
                                                 [](const model::Clip& candidate) { return model::clipId(candidate); });
        std::visit(
            core::Overloaded{[](model::AudioClip& audio) {
                                 throw AppError("clip " + audio.id.value + " is an audio clip, which has no notes");
                             },
                             edit},
            target);
    });
}

void forNotes(model::MidiClip& clip, const std::vector<model::NoteId>& ids,
              const std::function<void(model::Note&)>& edit) {
    std::ranges::for_each(
        clip.notes | std::views::filter([&](const model::Note& note) { return std::ranges::contains(ids, note.id); }),
        edit);
}

Json notesResult(const ProjectController& controller, const model::ClipId& clip,
                 const std::vector<model::NoteId>& ids) {
    const model::MidiClip& midi = midiClipOf(controller, clip);
    return {{"clip", clip.value},
            {"noteCount", midi.notes.size()},
            {"notes", midi.notes | std::views::filter([&](const model::Note& note) {
                          return std::ranges::contains(ids, note.id);
                      }) | std::views::transform(noteState) |
                          std::ranges::to<Json::array_t>()}};
}

}  // namespace emerick::app
