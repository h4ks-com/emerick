#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

#include "emerick/model/document.hpp"

namespace emerick::model {

// Format 2 added markers, format 3 track pan, fade shapes and fade curves, and format 4 MIDI notes apart from the other
// MIDI events, so older projects load with no markers, centered tracks, linear fades and notes gathered from events.
inline constexpr int kFormatVersion = 4;

nlohmann::json toJson(const Document& document);

// Throws std::runtime_error for an unknown format and nlohmann::json::exception for missing or mistyped fields.
Document documentFromJson(const nlohmann::json& json);

// A project is a folder holding project.json; audio files live in <folder>/audio/ and clips reference them relatively.
// Both throw std::runtime_error when the file cannot be read or written.
void saveProject(const Document& document, const std::filesystem::path& folder);
Document loadProject(const std::filesystem::path& folder);

}  // namespace emerick::model
