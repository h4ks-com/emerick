#pragma once

#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::app {

// Every project, track, clip, transport, device and export action, and the "project", "transport" and "devices"
// state providers.
void registerProjectActions(Registry& registry, ProjectController& controller, Nav& nav);

// The audio and MIDI track kinds with their add actions, and every track action.
void registerTrackActions(Registry& registry, ProjectController& controller, Nav& nav);
// Clip actions without a clip param act on the selected clips, so their shortcuts work on what the user selected.
void registerClipActions(Registry& registry, ProjectController& controller, Nav& nav);

}  // namespace emerick::app
