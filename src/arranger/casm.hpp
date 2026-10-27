#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "emerick/arranger/style.hpp"
#include "emerick/yamaha/section.hpp"

namespace emerick::arranger {

struct SectionName {
    yamaha::Section kind = yamaha::Section::Main;
    int variation = 0;
};

std::optional<SectionName> sectionName(std::string_view text);

ChannelRule defaultRule(int sourceChannel);

void readCasm(std::span<const std::uint8_t> body, Style& style);

}  // namespace emerick::arranger
