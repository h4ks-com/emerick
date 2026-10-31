#pragma once

#include <array>
#include <optional>
#include <string>

#include "emerick/app/registry.hpp"
#include "emerick/arranger/style.hpp"
#include "voice_list.hpp"

namespace emerick::yamaha {

// The voice name of each part that a pack instrument reports, by part.
using PackVoiceNames = std::array<std::optional<std::string>, arranger::kParts>;

// What arranger.style.notes returns: per section and part, the voice and the notes the pattern plays, with each drum
// key's General MIDI note through the voice list. Every section, or the one named ("Main D"), and every part that
// plays, or the one named ("Rhythm 1").
app::Json styleNotes(const arranger::Style& style, const VoiceList& list, const PackVoiceNames& packNames,
                     const std::optional<std::string>& section, const std::optional<std::string>& part);

}  // namespace emerick::yamaha
