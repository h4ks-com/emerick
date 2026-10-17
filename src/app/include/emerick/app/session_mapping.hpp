#pragma once

#include <filesystem>

#include "emerick/app/registry.hpp"
#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

float gainFromDb(double db);

// The building blocks a TrackType's toEngine starts from.
engine::Track audioEngineTrack(const model::Track& track, const std::filesystem::path& folder);
engine::Track midiEngineTrack(const model::Track& track);

// Each track plays through its TrackType; a kind without one stays silent. Relative clip files resolve against the
// project folder, and takes go to <folder>/audio.
engine::Session toSession(const model::Document& document, const Registry& registry,
                          const std::filesystem::path& folder);

}  // namespace emerick::app
