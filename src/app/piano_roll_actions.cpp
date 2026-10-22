#include <QStringList>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/note_edit.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/core/midi.hpp"
#include "emerick/core/overloaded.hpp"
#include "emerick/model/midi_notes.hpp"

namespace emerick::app {

namespace {

QStringList noteIdList(const std::vector<model::NoteId>& ids) {
    return ids | std::views::transform([](const model::NoteId& id) { return qs(id.value); }) |
           std::ranges::to<QStringList>();
}

Json pianoRollState(const Nav& nav) {
    const Json state = nav.toJson();
    return {{"pianoRollOpen", state.at("pianoRollOpen")},
            {"pianoRollClip", state.at("pianoRollClip")},
            {"selectedNotes", state.at("selectedNotes")}};
}

void openClip(ProjectController& controller, Nav& nav, const model::ClipId& clip) {
    midiClipOf(controller, clip);
    nav.setPianoRollClip(qs(clip.value));
    nav.setPianoRollOpen(true);
}

std::optional<model::ClipId> firstSelectedMidiClip(const ProjectController& controller, const Nav& nav) {
    const auto midi = [&](const model::ClipId& id) {
        return std::visit(core::Overloaded{[](const model::AudioClip&) { return false; },
                                           [](const model::MidiClip&) { return true; }},
                          *controller.project().findClip(id)->clip);
    };
    const auto ids = selectedClips(controller, nav);
    const auto found = std::ranges::find_if(ids, midi);
    return found == ids.end() ? std::nullopt : std::optional{*found};
}

// We keep the last output open, so a key's note-off reaches the port its note-on went to without reopening it.
struct Audition {
    std::string port;
    std::unique_ptr<engine::MidiOutput> output;
};

Json audition(ProjectController& controller, Audition& open, const Json& params) {
    const model::Track& track = *controller.project().track(trackParam(controller.project(), params));
    const int pitch = param<int>(params, "pitch");
    if (pitch < 0 || pitch > model::kMaxPitch) {
        throw AppError("pitch must be from 0 to 127");
    }
    const std::string& port = track.midiOut.portId;
    if (port.empty()) {
        return {{"sent", false}};
    }
    const bool on = optionalParam<bool>(params, "on").value_or(true);
    const int channel = std::max(1, track.midiOut.channel);
    const std::vector<std::uint8_t> bytes{
        core::midi::statusFor(on ? core::midi::kNoteOn : core::midi::kNoteOff, channel),
        static_cast<std::uint8_t>(pitch), static_cast<std::uint8_t>(on ? NoteDefaults{}.velocity : 0)};
    try {
        if (open.port != port || !open.output) {
            open.output = controller.engine().midi().openOutput(port);
            open.port = port;
        }
        open.output->send(bytes, 0);
    } catch (const std::runtime_error& error) {
        open = {};
        throw AppError(error.what());
    }
    return {{"sent", true}, {"port", port}, {"bytes", bytes}};
}

Json addMidiClip(const Registry& registry, ProjectController& controller, Nav& nav, const Json& params) {
    const model::TrackId id = targetTrack(controller, nav, params);
    const core::TempoMap& tempo = controller.project().document().tempo;
    model::MidiClip clip;
    clip.start = optionalPosition(params, "at", tempo).value_or(controller.positionTicks());
    clip.length = optionalParam<core::Ticks>(params, "length").value_or(tempo.ticksPerBar());
    if (clip.start < 0 || clip.length < 1) {
        throw AppError("at must be at or after bar 1 and length at least 1 tick");
    }
    if (!trackTakesClip(registry, *controller.project().track(id), clip)) {
        throw AppError("track " + controller.project().track(id)->name + " takes no MIDI clips");
    }
    controller.project().editTrack(id, "create MIDI clip",
                                   [&](model::Track& edited) { edited.clips.emplace_back(clip); });
    nav.setSelectedClips({qs(clip.id.value)});
    return clipResult(controller, clip.id);
}

void registerSettings(Registry& registry) {
    registry.addSetting({.id = kGridSetting,
                         .title = "Piano roll grid",
                         .aliases = {"note grid", "quantize value", "division"},
                         .category = "Settings / Editing",
                         .type = SettingType::Choice,
                         .defaultValue = 16,
                         .choices = {4, 8, 16, 32},
                         .hint = "Note value, as 1/N, that the piano roll snaps, nudges and quantizes to",
                         .location = "settings.general"});
    registry.addSetting({.id = "midi.pencil",
                         .title = "Pencil tool",
                         .aliases = {"draw notes"},
                         .category = "Settings / Editing",
                         .type = SettingType::Boolean,
                         .defaultValue = false,
                         .hint = "Click the piano roll grid to add a note; Command-click adds one with either tool",
                         .location = "settings.general"});
    registry.addAction(
        {.id = "midi.togglePencil",
         .title = "Toggle pencil tool",
         .aliases = {"pencil", "draw notes"},
         .category = "MIDI",
         .run = [&registry](const Json&) {
             return Json{
                 {"pencil", registry.setSetting("midi.pencil", !registry.settingValue("midi.pencil").get<bool>())}};
         }});
}

}  // namespace

void registerPianoRollActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registerSettings(registry);
    registry.addAction({.id = "view.togglePianoRoll",
                        .title = "Show or hide piano roll",
                        .aliases = {"piano roll", "editor", "note editor", "edit notes"},
                        .category = "View",
                        .shortcut = "P",
                        .extraShortcuts = {"E"},
                        .run =
                            [&controller, &nav](const Json&) {
                                if (nav.pianoRollOpen()) {
                                    nav.setPianoRollOpen(false);
                                } else if (const auto clip = firstSelectedMidiClip(controller, nav)) {
                                    openClip(controller, nav, *clip);
                                } else {
                                    nav.setPianoRollOpen(true);
                                }
                                return pianoRollState(nav);
                            },
                        .menus = {kViewMenu}});
    registry.addAction({.id = "midi.editClip",
                        .title = "Open in piano roll",
                        .aliases = {"edit MIDI clip", "show notes"},
                        .category = "MIDI",
                        .schema = {{"properties", {{"clip", {{"type", "string"}}}}}},
                        .run =
                            [&controller, &nav](const Json& params) {
                                const auto clip = params.contains("clip")
                                                      ? std::optional{clipParam(controller.project(), params)}
                                                      : firstSelectedMidiClip(controller, nav);
                                if (!clip) {
                                    throw AppError("select a MIDI clip first, or pass clip");
                                }
                                openClip(controller, nav, *clip);
                                return pianoRollState(nav);
                            },
                        .menus = {kClipContextMenu}});
    registry.addAction({.id = "midi.selectNotes",
                        .title = "Select notes",
                        .description = "Selects the notes in the piano roll's clip, or every note with all.",
                        .aliases = {"select all notes"},
                        .category = "MIDI",
                        .schema = {{"properties",
                                    {{"clip", {{"type", "string"}}},
                                     {"notes", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                                     {"all", {{"type", "boolean"}}}}}},
                        .run = [&controller, &nav](const Json& params) {
                            const model::ClipId clip = targetClip(controller, nav, params);
                            const bool all = optionalParam<bool>(params, "all").value_or(false);
                            std::vector<model::NoteId> ids;
                            if (all) {
                                ids = midiClipOf(controller, clip).notes | std::views::transform(&model::Note::id) |
                                      std::ranges::to<std::vector>();
                            } else if (params.contains("notes")) {
                                ids = targetNotes(controller, nav, clip, params);
                            }
                            nav.setPianoRollClip(qs(clip.value));
                            nav.setSelectedNotes(noteIdList(ids));
                            return pianoRollState(nav);
                        }});
    registry.addAction({.id = "midi.addClip",
                        .title = "Create MIDI clip",
                        .description = "Adds an empty MIDI clip to the track at at, by default the playhead, one bar "
                                       "long unless length says otherwise.",
                        .aliases = {"new MIDI region", "empty clip"},
                        .category = "MIDI",
                        .schema = {{"properties",
                                    {{"track", {{"type", "string"}}},
                                     {"at", Json::object()},
                                     {"length", {{"type", "integer"}, {"minimum", 1}}}}}},
                        .run = [&registry, &controller,
                                &nav](const Json& params) { return addMidiClip(registry, controller, nav, params); },
                        .menus = {kLaneContextMenu}});
    auto open = std::make_shared<Audition>();
    registry.addAction({.id = "midi.audition",
                        .title = "Play piano roll key",
                        .description = "Sends a note-on, or a note-off with on false, to the track's MIDI output "
                                       "when it has one.",
                        .category = "MIDI",
                        .schema = {{"required", {"track", "pitch"}},
                                   {"properties",
                                    {{"track", {{"type", "string"}}},
                                     {"pitch", {{"type", "integer"}}},
                                     {"on", {{"type", "boolean"}}}}}},
                        .run = [&controller, open](const Json& params) { return audition(controller, *open, params); },
                        .quiet = true});
}

void registerNoteClipboard(Registry& registry, ProjectController& controller, Nav& nav) {
    // The copy and paste handlers share the clipboard, and the registry owns the handlers.
    auto clipboard = std::make_shared<std::vector<model::Note>>();
    registry.addAction({.id = "midi.copyNotes",
                        .title = "Copy notes",
                        .category = "MIDI",
                        .schema = {{"properties",
                                    {{"clip", {{"type", "string"}}},
                                     {"notes", {{"type", "array"}, {"items", {{"type", "string"}}}}}}}},
                        .run = [&controller, &nav, clipboard](const Json& params) {
                            const model::ClipId clip = targetClip(controller, nav, params);
                            const auto ids = targetNotes(controller, nav, clip, params);
                            *clipboard = midiClipOf(controller, clip).notes | std::views::filter([&](const auto& note) {
                                             return std::ranges::contains(ids, note.id);
                                         }) |
                                         std::ranges::to<std::vector>();
                            return Json{{"copied", clipboard->size()}};
                        }});
    registry.addAction(
        {.id = "midi.pasteNotes",
         .title = "Paste notes",
         .description = "Pastes the copied notes with the earliest at at, in ticks from the clip start, or at the "
                        "playhead while it is inside the clip.",
         .category = "MIDI",
         .schema = {{"properties", {{"clip", {{"type", "string"}}}, {"at", {{"type", "integer"}}}}}},
         .run = [&controller, &nav, clipboard](const Json& params) {
             if (clipboard->empty()) {
                 throw AppError("copy notes first");
             }
             const model::ClipId clip = targetClip(controller, nav, params);
             const model::MidiClip& midi = midiClipOf(controller, clip);
             const core::Ticks earliest = std::ranges::min(*clipboard | std::views::transform(&model::Note::start));
             const core::Ticks playhead = controller.positionTicks() - midi.start;
             const core::Ticks at = optionalParam<core::Ticks>(params, "at")
                                        .value_or(playhead >= 0 && playhead < midi.length ? playhead : earliest);
             const auto pasted = *clipboard | std::views::transform([&](model::Note note) {
                 note.id = model::NoteId::make();
                 note.start += at - earliest;
                 return note;
             }) | std::views::filter([&](const model::Note& note) {
                 return note.start >= 0 && note.start < midi.length;
             }) | std::ranges::to<std::vector>();
             editNotes(controller, clip, "paste notes",
                       [&](model::MidiClip& edited) { std::ranges::copy(pasted, std::back_inserter(edited.notes)); });
             const auto ids = pasted | std::views::transform(&model::Note::id) | std::ranges::to<std::vector>();
             nav.setPianoRollClip(qs(clip.value));
             nav.setSelectedNotes(noteIdList(ids));
             return notesResult(controller, clip, ids);
         }});
}

}  // namespace emerick::app
