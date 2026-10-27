#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "emerick/yamaha/chord.hpp"
#include "emerick/yamaha/section.hpp"

namespace emerick::yamaha {

struct SectionPress {
    Section kind = Section::Main;
    int variation = 0;
    bool on = true;
    bool operator==(const SectionPress&) const = default;
};

// We pass both decoders the whole message, F0 and F7 included.
std::optional<Chord> decodeChordControl(std::span<const std::uint8_t> message);
std::optional<SectionPress> decodeSectionControl(std::span<const std::uint8_t> message);
std::vector<std::uint8_t> encodeChordControl(const Chord& chord);
// The section an External Controller button asks for, the buttons sectionPresses presses. Fill Up and Fill Down lead
// to the Main next to playingMain, and need one.
std::optional<SectionPress> decodeButtonPress(std::uint8_t cc, std::optional<int> playingMain);

}  // namespace emerick::yamaha
