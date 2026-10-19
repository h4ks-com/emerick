#pragma once

#include <filesystem>
#include <span>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/engine/audio_backend.hpp"
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

// One track routed to a device that is gone must not stop the others, so we clear its route for the engine and
// report it.
std::vector<engine::SessionError> dropMissingDevices(engine::Session& session,
                                                     std::span<const engine::AudioDeviceInfo> devices);
// Older builds could store a device id with text after it; a known id that starts the stored one replaces it.
void repairDeviceIds(model::Document& document, std::span<const engine::AudioDeviceInfo> devices);

}  // namespace emerick::app
