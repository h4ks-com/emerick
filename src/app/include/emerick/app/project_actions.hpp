#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::app {

class ViewEngine;

// Every project, track, clip, transport, device and export action, and the "project", "transport" and "devices"
// state providers.
// The tempo with the bpm, numerator and denominator params applied, or an AppError naming the valid range.
core::TempoMap tempoFromParams(const Json& params, core::TempoMap tempo);
void registerProjectActions(Registry& registry, ProjectController& controller, Nav& nav, ViewEngine& views);

// The audio and MIDI track kinds with their add actions, and every track action.
void registerTrackActions(Registry& registry, ProjectController& controller, Nav& nav);
// Rename, duplicate, delete, select, flags, gain, pan, color and icon. They act on the selected track without a track
// param.
void registerTrackEditActions(Registry& registry, ProjectController& controller, Nav& nav);
// Add, remove and edit sends, set a track's output bus, and create a bus that the selected tracks send to.
void registerSendActions(Registry& registry, ProjectController& controller, Nav& nav);
// Add, remove, move, bypass and edit insert effects on audio tracks and buses, and open their editor.
void registerInsertActions(Registry& registry, ProjectController& controller, Nav& nav);
// Points, lanes and modes of track automation, and the automation view. Point edits find their points in any lane.
void registerAutomationActions(Registry& registry, ProjectController& controller, Nav& nav);
// Deletes the track with every send and output bus that names it as one undo step.
void removeTrack(ProjectController& controller, const model::TrackId& id);
// The track named by params, or the selected track when params names none.
model::TrackId targetTrack(const ProjectController& controller, const Nav& nav, const Json& params);
// The target track, or with the selection param every selected track when the target is one of them, as in Logic.
std::vector<model::TrackId> targetTracks(const ProjectController& controller, const Nav& nav, const Json& params);
// Runs edit on every target track as one undo step named name, and returns the target track.
Json editTargets(ProjectController& controller, const Nav& nav, const Json& params, const std::string& name,
                 const std::function<void(const model::TrackId&)>& edit);
Json trackResult(const ProjectController& controller, const model::TrackId& id);
// Adds a MIDI track playing slot, monitored so it plays live input at once, with the default MIDI input and audio
// output unless params name others.
Json addInstrumentTrack(const Registry& registry, ProjectController& controller, Nav& nav, model::PluginSlot slot,
                        const Json& params);
// Plays the track through slot as one undo step, giving it the default audio output when it has none.
Json loadInstrument(const Registry& registry, ProjectController& controller, const model::TrackId& id,
                    model::PluginSlot slot);
Json editTrack(ProjectController& controller, const model::TrackId& id, std::string name,
               const std::function<void(model::Track&)>& edit);
// One submenu entry per track color, passing its index as the color param.
std::vector<MenuChoice> colorChoices();
// Clip actions without a clip param act on the selected clips, so their shortcuts work on what the user selected.
void registerClipActions(Registry& registry, ProjectController& controller, Nav& nav);
// Copy, cut, paste, select all and clip neighbors, and the Delete key's action.
void registerClipEditActions(Registry& registry, ProjectController& controller, Nav& nav);
// Fade lengths, shapes and curves, and crossfades between overlapping audio clips.
void registerFadeActions(Registry& registry, ProjectController& controller, Nav& nav);
// Looping MIDI clips from the toggle and from the loop handle's drag.
void registerClipLoopActions(Registry& registry, ProjectController& controller, Nav& nav);
// The piano roll and every note action. Without a clip param they act on the piano roll's clip, and without notes on
// its selected notes.
void registerNoteActions(Registry& registry, ProjectController& controller, Nav& nav);
// The clip named by params, or the selected clips when params names none.
std::vector<model::ClipId> targetClips(const ProjectController& controller, const Nav& nav, const Json& params);
Json clipResult(const ProjectController& controller, const model::ClipId& id);
Json clipResults(const ProjectController& controller, const std::vector<model::ClipId>& ids);

// The kind of clip a track type holds; a plugin lane holds none.
enum class ClipType : std::uint8_t { none, audio, midi };
ClipType clipTypeOf(const TrackType* type);
bool trackTakesClip(const Registry& registry, const model::Track& track, const model::Clip& clip);
// Deletes the clips as one undo step.
void deleteClips(ProjectController& controller, const std::vector<model::ClipId>& ids);
// The selected clip ids that still exist.
std::vector<model::ClipId> selectedClips(const ProjectController& controller, const Nav& nav);

// Add, rename, move, color, delete, select and go to markers. They act on the selected marker without a marker param.
void registerMarkerActions(Registry& registry, ProjectController& controller, Nav& nav);
// The selected marker when it still exists.
std::optional<model::MarkerId> selectedMarker(const ProjectController& controller, const Nav& nav);
void deleteMarker(ProjectController& controller, const model::MarkerId& id);

// Toggle the cycle, set its locators and set it to the selected clips.
void registerCycleActions(Registry& registry, ProjectController& controller, Nav& nav);

void registerProjectFileActions(Registry& registry, ProjectController& controller);
void registerProjectFolderActions(Registry& registry, ProjectController& controller, Nav& nav);
void registerExportActions(Registry& registry, ProjectController& controller, Nav& nav, ViewEngine& views);
void registerVideoActions(Registry& registry, ProjectController& controller, Nav& nav, ViewEngine& views);
// Compact reads for agents: midi.analyze and project.summary.
void registerSummaryActions(Registry& registry, ProjectController& controller);
// The Metronome settings section, the metronome actions and the "metronome" state provider. Every change to a
// metronome setting reaches the engine at the next flush.
void registerMetronome(Registry& registry, ProjectController& controller, Nav& nav);

}  // namespace emerick::app
