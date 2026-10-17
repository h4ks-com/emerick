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

// A settings page section at location settings.<key>, with its settings.show.<key> action; sections show in the
// order they are added.
void addSettingsSection(Registry& registry, Nav& nav, const std::string& key, const std::string& title);

}  // namespace emerick::app
