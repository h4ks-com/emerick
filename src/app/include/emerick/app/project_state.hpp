#pragma once

#include <span>
#include <string>

#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/core/time.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// Agents read these on every tool call, so MIDI clips carry an event count and only includeEvents adds the bytes.
// A length counts whole bars and beats from zero, so two beats read "0.2.0".
std::string durationText(core::Ticks ticks, const core::TempoMap& tempo);
Json positionState(core::Ticks ticks, const core::TempoMap& tempo);
Json clipState(const model::Clip& clip, const core::TempoMap& tempo, bool includeEvents = false);
Json trackState(const model::Track& track, const core::TempoMap& tempo,
                std::span<const engine::AudioDeviceInfo> devices = {});
// The device name and channels, such as "MOTU M2 1-2", or the id of a device that is gone.
std::string audioRouteText(const model::AudioRoute& route, std::span<const engine::AudioDeviceInfo> devices);
Json tempoState(const core::TempoMap& tempo);
// Number counts the markers from 1 in position order, as the go to marker keys do.
Json markerState(const model::Marker& marker, int number, const core::TempoMap& tempo);
Json markersState(const model::Document& document);
// Sends a pending edit to the engine first, so sessionError and skippedClips describe the current project.
Json projectState(ProjectController& controller);
Json transportState(const ProjectController& controller);
Json devicesState(const ProjectController& controller);
// Reads "11.6 ms round trip, 5.8 ms output", as Logic shows it.
std::string latencyText(const engine::StreamLayout& layout);

// What the route pickers list: None and each device by name, with the params that select it. An audio device also
// lists its channel options, and midiChannels lists All and 1 to 16.
Json routeDevices(const Json& devices);

}  // namespace emerick::app
