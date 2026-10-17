#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/yamaha/raw_event.hpp"

namespace emerick::yamaha {

struct Part {
    int channel = 1;
    std::string_view name;
    std::string_view icon;
};

// Channels 1 to 16 as the Genos2 transmits them; icons are Phosphor names.
const std::array<Part, 16>& genos2Parts();

struct Voice {
    int channel = 1;
    int bankMsb = 0;
    int bankLsb = 0;
    int program = 1;
    int volume = 100;
    int pan = 64;
    std::string name;
    bool operator==(const Voice&) const = default;
};

// Lines read "channel msb lsb program(1-128) volume pan name"; blank lines and # lines are skipped.
std::vector<Voice> parseVoices(std::string_view text);
std::vector<Voice> loadVoices(const std::filesystem::path& path);

// Bank MSB, LSB, Program Change, Volume and Pan per voice, gapTicks apart.
std::vector<RawEvent> voiceMessages(const std::vector<Voice>& voices, core::Ticks at, core::Ticks gapTicks);

}  // namespace emerick::yamaha
