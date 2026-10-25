#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::app {

inline constexpr const char* kGridSetting = "midi.grid";

// What a new note gets when the caller names no length or velocity: the last ones the user set.
struct NoteDefaults {
    core::Ticks length = 0;
    int velocity = 100;
};

// The piano roll grid in ticks, from the midi.grid setting's note value.
core::Ticks gridTicks(const Registry& registry);

// The clip params names, or else the clip the piano roll edits.
model::ClipId targetClip(const ProjectController& controller, const Nav& nav, const Json& params);
const model::MidiClip& midiClipOf(const ProjectController& controller, const model::ClipId& id);

enum class AllWhenNone : std::uint8_t { no, yes };
// The notes params names, or else the selected notes; with AllWhenNone::yes, every note when none is selected.
std::vector<model::NoteId> targetNotes(const ProjectController& controller, const Nav& nav, const model::ClipId& clip,
                                       const Json& params, AllWhenNone all = AllWhenNone::no);

// Edits the MIDI clip as one undo step named name.
void editNotes(ProjectController& controller, const model::ClipId& clip, const std::string& name,
               const std::function<void(model::MidiClip&)>& edit);
void forNotes(model::MidiClip& clip, const std::vector<model::NoteId>& ids,
              const std::function<void(model::Note&)>& edit);
// The clip id with the state of the notes among ids that still exist.
Json notesResult(const ProjectController& controller, const model::ClipId& clip, const std::vector<model::NoteId>& ids);

// The grid and pencil settings, opening and closing the piano roll, note selection and auditioning a key.
void registerPianoRollActions(Registry& registry, ProjectController& controller, Nav& nav);
// Copy and paste of notes within a clip.
void registerNoteClipboard(Registry& registry, ProjectController& controller, Nav& nav);
// The controller lanes under the piano roll notes and the edits of their values.
void registerControlActions(Registry& registry, ProjectController& controller, Nav& nav);
// The lanes the piano roll offers for the clip as {id, title, hasData}: the standard lanes, then any other controller
// the clip holds.
Json laneChoices(const model::MidiClip& clip);

}  // namespace emerick::app
