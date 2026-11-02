#include <algorithm>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/note_edit.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/model/midi_notes.hpp"

namespace emerick::app {

namespace {

constexpr int kMaxChannel = 16;
constexpr int kOctave = 12;

Json noteSchema() {
    return {{"clip", {{"type", "string"}}}, {"notes", {{"type", "array"}, {"items", {{"type", "string"}}}}}};
}

Json withNoteSchema(Json properties) {
    properties.update(noteSchema());
    return properties;
}

int checkedPitch(int pitch) {
    if (pitch < 0 || pitch > model::kMaxPitch) {
        throw AppError("pitch must be from 0 to 127, got " + std::to_string(pitch));
    }
    return pitch;
}

Json notePropertiesSchema() {
    return {{"pitch", {{"type", "integer"}, {"minimum", 0}, {"maximum", model::kMaxPitch}}},
            {"start", {{"type", "integer"}}},
            {"length", {{"type", "integer"}}},
            {"velocity", {{"type", "integer"}, {"minimum", 1}, {"maximum", model::kMaxVelocity}}},
            {"channel",
             {{"type", "integer"},
              {"minimum", 1},
              {"maximum", kMaxChannel},
              {"description", "default 1; General MIDI plays drums on channel 10"}}}};
}

Json addNoteSchema() {
    Json properties = notePropertiesSchema();
    properties["clip"] = {{"type", "string"}};
    properties["notes"] = {{"type", "array"},
                           {"description", "notes to add in place of pitch and start, each with the params of one"},
                           {"items", {{"type", "object"}, {"properties", notePropertiesSchema()}}}};
    return properties;
}

// Reads one new note and makes its length and velocity the defaults for the next one.
model::Note newNote(const model::MidiClip& midi, NoteDefaults& defaults, const Json& params) {
    const auto start = param<core::Ticks>(params, "start");
    if (start < 0 || start >= midi.length) {
        throw AppError("start must be inside the clip, from 0 to " + std::to_string(midi.length - 1) + " ticks");
    }
    const auto length = optionalParam<core::Ticks>(params, "length").value_or(defaults.length);
    if (length < 1) {
        throw AppError("length must be at least 1 tick");
    }
    const int velocity = optionalParam<int>(params, "velocity").value_or(defaults.velocity);
    const int channel = optionalParam<int>(params, "channel").value_or(1);
    if (velocity < 1 || velocity > model::kMaxVelocity || channel < 1 || channel > kMaxChannel) {
        throw AppError("velocity must be from 1 to 127 and channel from 1 to 16");
    }
    defaults = {.length = length, .velocity = velocity};
    return {.pitch = checkedPitch(param<int>(params, "pitch")),
            .velocity = velocity,
            .start = start,
            .length = length,
            .channel = channel};
}

Json addNotes(ProjectController& controller, Nav& nav, NoteDefaults& defaults, const Json& params,
              const std::vector<Json>& given, const std::string& name) {
    const model::ClipId clip = targetClip(controller, nav, params);
    const model::MidiClip& midi = midiClipOf(controller, clip);
    NoteDefaults next = defaults;
    const std::vector<model::Note> notes =
        given | std::views::transform([&](const Json& note) { return newNote(midi, next, note); }) |
        std::ranges::to<std::vector>();
    editNotes(controller, clip, name,
              [&](model::MidiClip& edited) { edited.notes.insert(edited.notes.end(), notes.begin(), notes.end()); });
    defaults = next;
    const std::vector<model::NoteId> ids =
        notes | std::views::transform(&model::Note::id) | std::ranges::to<std::vector>();
    if (nav.pianoRollClip() == qs(clip.value)) {
        nav.setSelectedNotes(ids | std::views::transform([](const model::NoteId& id) { return qs(id.value); }) |
                             std::ranges::to<QStringList>());
    }
    return notesResult(controller, clip, ids);
}

// Moves the notes by ticks and semitones, held back where a note would leave the clip or the keyboard.
void moveNotes(model::MidiClip& clip, const std::vector<model::NoteId>& ids, core::Ticks by, int semitones) {
    auto moved =
        clip.notes | std::views::filter([&](const model::Note& note) { return std::ranges::contains(ids, note.id); });
    for (const model::Note& note : moved) {
        by = std::clamp(by, -note.start, clip.length - 1 - note.start);
        semitones = std::clamp(semitones, -note.pitch, model::kMaxPitch - note.pitch);
    }
    for (model::Note& note : moved) {
        note.start += by;
        note.pitch += semitones;
    }
}

Json transpose(ProjectController& controller, const Nav& nav, const Json& params) {
    const model::ClipId clip = targetClip(controller, nav, params);
    const auto ids = targetNotes(controller, nav, clip, params, AllWhenNone::yes);
    const int semitones = param<int>(params, "semitones");
    editNotes(controller, clip, "transpose notes", [&](model::MidiClip& edited) {
        forNotes(edited, ids, [&](model::Note& note) {
            if (note.pitch + semitones < 0 || note.pitch + semitones > model::kMaxPitch) {
                throw AppError("transposing by " + std::to_string(semitones) + " takes pitch " +
                               std::to_string(note.pitch) + " outside 0 to 127");
            }
            note.pitch += semitones;
        });
    });
    return notesResult(controller, clip, ids);
}

Json quantize(const Registry& registry, ProjectController& controller, const Nav& nav, const Json& params) {
    const model::ClipId clip = targetClip(controller, nav, params);
    const auto ids = targetNotes(controller, nav, clip, params, AllWhenNone::yes);
    const auto grid = optionalParam<core::Ticks>(params, "grid").value_or(gridTicks(registry));
    const double strength = std::clamp(optionalParam<double>(params, "strength").value_or(1.0), 0.0, 1.0);
    editNotes(controller, clip, "quantize notes", [&](model::MidiClip& edited) {
        forNotes(edited, ids, [&](model::Note& note) {
            note.start = std::clamp<core::Ticks>(model::quantized(note.start, grid, strength), 0, edited.length - 1);
        });
    });
    return notesResult(controller, clip, ids);
}

void registerNoteEdits(Registry& registry, ProjectController& controller, Nav& nav,
                       const std::shared_ptr<NoteDefaults>& defaults) {
    registry.addAction({.id = "midi.addNote",
                        .title = "Add note",
                        .description =
                            "Adds a note to the MIDI clip, or with notes a list of them as one undo step. Start and "
                            "length are ticks from the clip start; length and velocity default to the last note's.",
                        .aliases = {"draw note", "pencil", "add notes"},
                        .category = "MIDI",
                        .schema = {{"properties", addNoteSchema()}},
                        .run = [&controller, &nav, defaults](const Json& params) {
                            if (!params.contains("notes")) {
                                return addNotes(controller, nav, *defaults, params, {params}, "add note");
                            }
                            return addNotes(controller, nav, *defaults, params,
                                            param<std::vector<Json>>(params, "notes"), "add notes");
                        }});
    registry.addAction(
        {.id = "midi.moveNotes",
         .title = "Move notes",
         .description = "Moves the notes by ticks and semitones; the move stops where a note would leave the clip or "
                        "the keyboard.",
         .aliases = {"nudge notes", "drag notes"},
         .category = "MIDI",
         .schema = {{"properties", withNoteSchema({{"by", {{"type", "integer"}}}, {"pitch", {{"type", "integer"}}}})}},
         .run = [&controller, &nav](const Json& params) {
             const model::ClipId clip = targetClip(controller, nav, params);
             const auto ids = targetNotes(controller, nav, clip, params);
             const auto by = optionalParam<core::Ticks>(params, "by").value_or(0);
             const int pitch = optionalParam<int>(params, "pitch").value_or(0);
             editNotes(controller, clip, "move notes",
                       [&](model::MidiClip& edited) { moveNotes(edited, ids, by, pitch); });
             return notesResult(controller, clip, ids);
         }});
    registry.addAction(
        {.id = "midi.resizeNotes",
         .title = "Resize notes",
         .description = "Sets the notes' length in ticks, or changes it by ticks; a note lasts at least 1 tick.",
         .aliases = {"note length", "lengthen notes", "shorten notes"},
         .category = "MIDI",
         .schema = {{"properties", withNoteSchema({{"length", {{"type", "integer"}}}, {"by", {{"type", "integer"}}}})}},
         .run = [&controller, &nav, defaults](const Json& params) {
             const model::ClipId clip = targetClip(controller, nav, params);
             const auto ids = targetNotes(controller, nav, clip, params);
             const auto length = optionalParam<core::Ticks>(params, "length");
             const auto by = optionalParam<core::Ticks>(params, "by");
             if (!length && !by) {
                 throw AppError("pass length or by");
             }
             editNotes(controller, clip, "resize notes", [&](model::MidiClip& edited) {
                 forNotes(edited, ids, [&](model::Note& note) {
                     note.length = std::max<core::Ticks>(1, length.value_or(note.length) + by.value_or(0));
                     defaults->length = note.length;
                 });
             });
             return notesResult(controller, clip, ids);
         }});
    registry.addAction(
        {.id = "midi.setVelocity",
         .title = "Set note velocity",
         .description = "Sets the notes' velocity from 1 to 127, or changes it by an amount.",
         .aliases = {"velocity", "note volume", "louder notes", "softer notes"},
         .category = "MIDI",
         .schema = {{"properties",
                     withNoteSchema({{"velocity", {{"type", "integer"}, {"minimum", 1}, {"maximum", 127}}},
                                     {"by", {{"type", "integer"}}}})}},
         .run = [&controller, &nav, defaults](const Json& params) {
             const model::ClipId clip = targetClip(controller, nav, params);
             const auto ids = targetNotes(controller, nav, clip, params);
             const auto velocity = optionalParam<int>(params, "velocity");
             const auto by = optionalParam<int>(params, "by");
             if (!velocity && !by) {
                 throw AppError("pass velocity or by");
             }
             editNotes(controller, clip, "set note velocity", [&](model::MidiClip& edited) {
                 forNotes(edited, ids, [&](model::Note& note) {
                     note.velocity =
                         std::clamp(velocity.value_or(note.velocity) + by.value_or(0), 1, model::kMaxVelocity);
                     defaults->velocity = note.velocity;
                 });
             });
             return notesResult(controller, clip, ids);
         }});
    registry.addAction({.id = "midi.deleteNotes",
                        .title = "Delete notes",
                        .aliases = {"remove notes", "erase notes"},
                        .category = "MIDI",
                        .schema = {{"properties", noteSchema()}},
                        .run = [&controller, &nav](const Json& params) {
                            const model::ClipId clip = targetClip(controller, nav, params);
                            const auto ids = targetNotes(controller, nav, clip, params);
                            editNotes(controller, clip, "delete notes", [&](model::MidiClip& edited) {
                                std::erase_if(edited.notes, [&](const model::Note& note) {
                                    return std::ranges::contains(ids, note.id);
                                });
                            });
                            return notesResult(controller, clip, {});
                        }});
    registry.addAction(
        {.id = "midi.quantize",
         .title = "Quantize notes",
         .description = "Moves the starts of the selected notes, or of every note in the clip, toward the grid. "
                        "Strength 1 puts them on the grid and 0.5 moves them halfway; grid is in ticks and defaults to "
                        "the piano roll grid.",
         .aliases = {"quantise", "snap notes to grid", "fix timing"},
         .category = "MIDI",
         .shortcut = "Q",
         .schema = {{"properties", withNoteSchema({{"strength", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                                                   {"grid", {{"type", "integer"}, {"minimum", 1}}}})}},
         .run = [&registry, &controller,
                 &nav](const Json& params) { return quantize(registry, controller, nav, params); },
         .menus = {kEditMenu}});
    registry.addAction(
        {.id = "midi.transpose",
         .title = "Transpose notes",
         .description = "Moves the selected notes, or every note in the clip, by semitones; a pitch outside 0 to 127 "
                        "refuses the whole edit.",
         .aliases = {"pitch shift notes", "octave up", "octave down"},
         .category = "MIDI",
         .schema = {{"required", {"semitones"}},
                    {"properties", withNoteSchema({{"semitones", {{"type", "integer"}}}})}},
         .run = [&controller, &nav](const Json& params) { return transpose(controller, nav, params); },
         .menus = {kEditMenu},
         .submenu =
             [] {
                 return std::vector<MenuChoice>{
                     {.id = "octaveUp", .title = "Octave up", .params = {{"semitones", kOctave}}},
                     {.id = "semitoneUp", .title = "Semitone up", .params = {{"semitones", 1}}},
                     {.id = "semitoneDown", .title = "Semitone down", .params = {{"semitones", -1}}},
                     {.id = "octaveDown", .title = "Octave down", .params = {{"semitones", -kOctave}}},
                 };
             }});
}

}  // namespace

void registerNoteActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registerPianoRollActions(registry, controller, nav);
    auto defaults = std::make_shared<NoteDefaults>(NoteDefaults{.length = gridTicks(registry)});
    registerNoteEdits(registry, controller, nav, defaults);
    registerNoteClipboard(registry, controller, nav);
    registerControlActions(registry, controller, nav);
}

}  // namespace emerick::app
