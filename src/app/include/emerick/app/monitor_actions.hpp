#pragma once

#include <memory>

#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/app/spectrogram.hpp"
#include "emerick/app/tuner.hpp"

namespace emerick::app {

struct Monitors {
    Monitors(Registry& registry, ProjectController& controller, Nav& nav)
        : tuner(registry, controller, nav), spectrogram(registry, controller, nav) {}

    Tuner tuner;
    Spectrogram spectrogram;
};

// Registers the tuner and spectrogram settings, creates both monitors, and registers the actions that open their
// windows and pick their sources, and the "tuner" and "spectrogram" state.
std::unique_ptr<Monitors> registerMonitors(Registry& registry, ProjectController& controller, Nav& nav);

}  // namespace emerick::app
