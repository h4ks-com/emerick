#pragma once

#include <memory>

#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/yamaha/chord_input.hpp"

namespace emerick::yamaha {

// The Chords and Style track kinds, the yamaha.* actions and settings, and the Digital Keyboard MIDI profile. The
// caller keeps the returned chord input for as long as the registry runs.
[[nodiscard]] std::unique_ptr<ChordInput> registerWith(app::Registry& registry, app::ProjectController& controller,
                                                       app::Nav& nav);

}  // namespace emerick::yamaha
