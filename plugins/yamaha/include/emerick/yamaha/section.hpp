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
    std::optional<std::uint8_t> sysexFamily;
    std::uint8_t controllerCc = 0;
};

// We put the variation's character where display says {}. The keyboard reports a section as the Section Control
// switch sysexFamily plus the variation, and we press its External Controller button controllerCc plus the variation.
// A Fill has no switch of its own: the keyboard reports it as the fill of the Main that is playing. A FillTo has no
// button of its own: we press Fill Up, Fill Down or the button of the Main it leads to.
inline constexpr std::array kSections{
    SectionInfo{Section::Main, "main", "Main {}", "ABCD", 0x08, 11},
    SectionInfo{Section::Fill, "fill", "Fill", "", std::nullopt, 6},
    SectionInfo{Section::FillTo, "fillTo", "Main {} (fill)", "ABCD", 0x10, 11},
    SectionInfo{Section::Break, "break", "Break", "", 0x18, 8},
    SectionInfo{Section::Intro, "intro", "Intro {}", "123", 0x00, 21},
    SectionInfo{Section::Ending, "ending", "Ending {}", "123", 0x20, 31},
};
inline constexpr std::uint8_t kFillDownCc = 5;
inline constexpr std::uint8_t kFillUpCc = 9;
inline constexpr std::uint8_t kStartStopCc = 0;
static_assert(kSections.size() == static_cast<std::size_t>(Section::Ending) + 1);
static_assert(std::ranges::all_of(std::views::iota(std::size_t{0}, kSections.size()), [](std::size_t index) {
    return kSections[index].kind == static_cast<Section>(index);
}));

constexpr const SectionInfo& sectionInfo(Section kind) { return kSections[static_cast<std::size_t>(kind)]; }

// A Main, Intro or Ending begins at a bar line and plays a pattern; a fill or a break leads into one.
constexpr bool startsSection(Section kind) {
    return kind == Section::Main || kind == Section::Intro || kind == Section::Ending;
}

constexpr int variationCount(const SectionInfo& info) { return std::max(1, static_cast<int>(info.variations.size())); }

}  // namespace emerick::yamaha
