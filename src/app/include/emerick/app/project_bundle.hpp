#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "emerick/model/document.hpp"

namespace emerick::app {

// Emerick in the platform music folder.
std::filesystem::path defaultProjectsFolder();
// Absolute and lexically normal, with no trailing separator.
std::filesystem::path normalizedPath(const std::filesystem::path& path);
// Takes a bundle or the project.json inside it.
std::filesystem::path bundleOf(const std::filesystem::path& path);
// Adds the .emerick suffix when the path lacks it.
std::filesystem::path namedBundle(const std::filesystem::path& path);
// The first "Untitled N.emerick" in folder that does not exist yet.
std::filesystem::path nextUntitledBundle(const std::filesystem::path& folder);
bool holdsNothing(const model::Document& document, const std::filesystem::path& bundle);
// Throws std::filesystem::filesystem_error.
void moveBundle(const std::filesystem::path& from, const std::filesystem::path& to);
// Deletes the files in <bundle>/audio that no clip uses and returns their sorted names. Throws
// std::filesystem::filesystem_error.
std::vector<std::string> removeUnusedAudio(const model::Document& document, const std::filesystem::path& bundle);

}  // namespace emerick::app
