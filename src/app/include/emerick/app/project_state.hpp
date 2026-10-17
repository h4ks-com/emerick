#pragma once

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
Json trackState(const model::Track& track, const core::TempoMap& tempo);
Json tempoState(const core::TempoMap& tempo);
Json projectState(const ProjectController& controller);
Json transportState(const ProjectController& controller);
Json devicesState(ProjectController& controller);

// What the route pickers show: the current route as text, and each choice with the params that select it.
std::string routeText(const model::AudioRoute& route);
std::string routeText(const model::MidiRoute& route);
Json routeChoices(const Json& devices);

}  // namespace emerick::app
