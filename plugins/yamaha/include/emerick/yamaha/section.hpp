#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string_view>

namespace emerick::yamaha {

enum class Section : std::uint8_t { Main, Fill, FillTo, Break, Intro, Ending };

struct SectionInfo {
    Section kind = Section::Main;
    std::string_view json;
    std::string_view display;
    std::string_view variations;
    int ccBase = 0;
    std::optional<std::uint8_t> sysexFamily;
};

// We put the variation's character where display says {}, and the External Controller button is ccBase plus the
// variation.
inline constexpr std::array kSections{
    SectionInfo{Section::Main, "main", "Main {}", "ABCD", 11, 0x08},
    SectionInfo{Section::Fill, "fill", "Fill", "", 6, std::nullopt},
    SectionInfo{Section::FillTo, "fillTo", "Main {} (fill)", "ABCD", 11, 0x10},
    SectionInfo{Section::Break, "break", "Break", "", 8, 0x18},
    SectionInfo{Section::Intro, "intro", "Intro {}", "123", 21, 0x00},
    SectionInfo{Section::Ending, "ending", "Ending {}", "123", 31, 0x20},
};
static_assert(kSections.size() == static_cast<std::size_t>(Section::Ending) + 1);
static_assert(std::ranges::all_of(std::views::iota(std::size_t{0}, kSections.size()), [](std::size_t index) {
    return kSections[index].kind == static_cast<Section>(index);
}));

constexpr const SectionInfo& sectionInfo(Section kind) { return kSections[static_cast<std::size_t>(kind)]; }

constexpr int variationCount(const SectionInfo& info) { return std::max(1, static_cast<int>(info.variations.size())); }

}  // namespace emerick::yamaha
