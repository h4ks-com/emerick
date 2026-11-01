#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// The document holds absolute paths in our bundled plugins' state, and project.json holds the ones inside the folder
// the bundle sits in relative to the bundle, so a project moved with its files still finds them.
model::Document withRelativePluginPaths(model::Document document, const std::filesystem::path& bundle);
model::Document withAbsolutePluginPaths(model::Document document, const std::filesystem::path& bundle);

struct CollectedFiles {
    std::vector<std::string> copied;
    std::vector<std::string> missing;
};

// Copies every file the project names outside its bundle into the bundle, audio into audio/ and the rest into files/,
// and points the project at the copies as one undo step. Throws AppError when an agent may not read a file or write
// its copy.
CollectedFiles collectFiles(Registry& registry, ProjectController& controller);

}  // namespace emerick::app
