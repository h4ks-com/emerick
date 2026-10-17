#pragma once

#include <memory>
#include <string_view>

#include "emerick/app/registry.hpp"
#include "emerick/engine/audio_backend.hpp"
#include "emerick/engine/midi_backend.hpp"

namespace emerick::app {

// midi.inject and midi.sent, which feed and read the virtual ports for tests and agents.
void registerVirtualMidiActions(Registry& registry, engine::VirtualMidiBackend& midi);

// "null" or "default" (miniaudio); throws AppError for any other name.
std::unique_ptr<engine::AudioBackend> makeAudioBackend(std::string_view name);
// "virtual" or "default" (libremidi); throws AppError for any other name.
std::unique_ptr<engine::MidiBackend> makeMidiBackend(std::string_view name);

}  // namespace emerick::app
