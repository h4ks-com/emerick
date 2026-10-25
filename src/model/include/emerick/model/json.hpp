#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

#include "emerick/model/document.hpp"

namespace emerick::model {

// Format 2 added markers, format 3 track pan, fade shapes and fade curves, format 4 buses, sends and output buses,
// format 5 MIDI notes apart from the other MIDI events, format 6 automation, format 7 insert effects, format 8 clip
// mute and track record delay and format 9 the cycle, so older projects load with no markers, centered tracks, linear
// fades, no sends, notes gathered from events, no automation in Read mode, no inserts, every clip playing, no record
// delay and no cycle.
inline constexpr int kFormatVersion = 9;

nlohmann::json toJson(const Document& document);

// Throws std::runtime_error for an unknown format and nlohmann::json::exception for missing or mistyped fields.
Document documentFromJson(const nlohmann::json& json);

// A project is a folder holding project.json; audio files live in <folder>/audio/ and clips reference them relatively.
// Both throw std::runtime_error when the file cannot be read or written.
void saveProject(const Document& document, const std::filesystem::path& folder);
Document loadProject(const std::filesystem::path& folder);

}  // namespace emerick::model
