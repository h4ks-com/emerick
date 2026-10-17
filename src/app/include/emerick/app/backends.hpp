#pragma once

#include <memory>
#include <string_view>

#include "emerick/engine/audio_backend.hpp"
#include "emerick/engine/midi_backend.hpp"

namespace emerick::app {

// "null" or "default" (miniaudio); throws AppError for any other name.
std::unique_ptr<engine::AudioBackend> makeAudioBackend(std::string_view name);
// "virtual" or "default" (libremidi); throws AppError for any other name.
std::unique_ptr<engine::MidiBackend> makeMidiBackend(std::string_view name);

}  // namespace emerick::app
