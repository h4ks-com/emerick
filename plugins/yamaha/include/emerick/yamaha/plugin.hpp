#pragma once

#include <memory>

#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/yamaha/chord_input.hpp"

namespace emerick::yamaha {

class KeyboardCheck;
class StyleSync;

struct Listeners {
    Listeners(app::Registry& registry, app::ProjectController& controller, app::Nav& nav);
    ~Listeners();
    Listeners(const Listeners&) = delete;
    Listeners& operator=(const Listeners&) = delete;
    Listeners(Listeners&&) = delete;
    Listeners& operator=(Listeners&&) = delete;

    ChordInput chordInput;
    std::unique_ptr<KeyboardCheck> keyboardCheck;
    std::unique_ptr<StyleSync> styleSync;
};

// The Chords and Style track kinds, the yamaha.* actions and settings, and the Digital Keyboard MIDI profile. The
// caller keeps the returned listeners for as long as the registry runs.
[[nodiscard]] std::unique_ptr<Listeners> registerWith(app::Registry& registry, app::ProjectController& controller,
                                                      app::Nav& nav);

}  // namespace emerick::yamaha
