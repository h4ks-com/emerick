#pragma once

#include <span>
#include <string>

#include "emerick/app/project_controller.hpp"
#include "emerick/app/project_view.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/core/time.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// Agents read these on every tool call, so MIDI clips carry note and event counts and only includeEvents adds the
// notes and the event bytes.
Json positionState(core::Ticks ticks, const core::TempoMap& tempo);
Json noteState(const model::Note& note);
Json clipState(const model::Clip& clip, const core::TempoMap& tempo, bool includeEvents = false);
Json trackState(const model::Track& track, const core::TempoMap& tempo,
                std::span<const engine::AudioDeviceInfo> devices = {});
// The device name and channels, such as "MOTU M2 1-2", or the id of a device that is gone.
std::string audioRouteText(const model::AudioRoute& route, std::span<const engine::AudioDeviceInfo> devices);
Json tempoState(const core::TempoMap& tempo);
// Sends a pending edit to the engine first, so sessionError and skippedClips describe the current project.
Json projectState(ProjectController& controller);
Json cycleState(const model::Document& document);
Json transportState(const ProjectController& controller);
Json devicesState(const ProjectController& controller);
// The MIDI output ports as the {id, name} entries a String setting suggests.
Json midiOutputChoices(const ProjectController& controller);
// Reads "11.6 ms round trip, 5.8 ms output", as Logic shows it.
std::string latencyText(const engine::StreamLayout& layout);

// What the route pickers list: None and each device by name, with the params that select it. An audio device also
// lists its channel options, and midiChannels lists All and 1 to 16.
Json routeDevices(const Json& devices);

}  // namespace emerick::app
