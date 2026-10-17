#pragma once

#include <string>

#include "emerick/app/nav.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::app {

struct Backends {
    std::string audio;
    std::string midi;
};

const char* appVersion();

// The app's own actions, settings, UI locations and the "nav" and "app" state providers.
void registerCoreActions(Registry& registry, Nav& nav, const Backends& backends);

}  // namespace emerick::app
