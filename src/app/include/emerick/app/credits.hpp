#pragma once

#include <string>
#include <string_view>

#include "emerick/app/libraries.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::app {

// The license of a plugin Emerick ships, or empty for another plugin.
std::string pluginLicense(std::string_view pluginId);

// Adds project.credits, which credits what the open project plays, and catalog.export, which writes every sound Emerick
// can play with its license.
void registerCreditActions(Registry& registry, ProjectController& controller, const Libraries& libraries);

}  // namespace emerick::app
