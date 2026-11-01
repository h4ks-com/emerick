#pragma once

#include <functional>
#include <string>
#include <vector>

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

// Callers' designated initializers skip the defaulted fields, which this check calls redundant.
// NOLINTBEGIN(readability-redundant-member-init)
struct SettingsSection {
    std::string key;
    std::string title;
    // The sidebar heading the section lists under, such as kPluginsGroup; empty lists it with the built-in sections.
    std::string group = {};
    std::string shortcut = {};
    // Menus that list the section's show action, which then takes the section title as its title.
    std::vector<std::string> menus = {};
};
// NOLINTEND(readability-redundant-member-init)

inline constexpr const char* kPluginsGroup = "Plugins";

// A settings page section at location settings.<key>, with its settings.show.<key> action. Sections show in the order
// they are added, the ungrouped ones first and then each group under its heading.
void addSettingsSection(Registry& registry, Nav& nav, const SettingsSection& section);

// The merge of a String setting that holds a file path, which checks an agent may read the file; empty stays empty.
std::function<Json(const Json&, const Json&)> readablePath(const Registry& registry);
// The merge of a List setting of folders, which checks an agent may read each one.
std::function<Json(const Json&, const Json&)> readableFolders(const Registry& registry);

// Adds a String setting that holds such a path, with a <id>.choose action the settings page offers as a button that
// opens a file dialog in the given mode.
void addPathSetting(Registry& registry, Setting setting, FileDialogMode mode, const std::string& nameFilter = {});

}  // namespace emerick::app
