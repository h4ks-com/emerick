#pragma once

#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::yamaha {

// The Chords and Style track kinds, the yamaha.* actions and settings, and the Digital Keyboard MIDI profile.
void registerWith(app::Registry& registry, app::ProjectController& controller, app::Nav& nav);

}  // namespace emerick::yamaha
