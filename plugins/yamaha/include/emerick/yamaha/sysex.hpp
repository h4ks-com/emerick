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

}  // namespace emerick::yamaha
