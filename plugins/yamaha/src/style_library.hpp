#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/arranger/style.hpp"
#include "emerick/core/time.hpp"
#include "emerick/model/document.hpp"
#include "emerick/yamaha/section.hpp"

namespace emerick::yamaha {

inline constexpr const char* kArrangerPluginId = "org.emerick.arranger";
inline constexpr const char* kVoicesPluginId = "org.emerick.voices";
inline constexpr const char* kStyleFoldersSetting = "yamaha.styleFolders";
inline constexpr const char* kVoiceListSetting = "yamaha.voiceList";
inline constexpr std::array<std::string_view, 3> kStyleExtensions{"sty", "prs", "sst"};

// The sections of a style as the inspector lists them, in the order a song plays them.
struct StyleSlot {
    Section kind = Section::Main;
    int variation = 0;
    std::string_view label;
};
inline constexpr std::array kStyleSlots{
    StyleSlot{Section::Intro, 0, "I1"},  StyleSlot{Section::Intro, 1, "I2"},  StyleSlot{Section::Intro, 2, "I3"},
    StyleSlot{Section::Main, 0, "A"},    StyleSlot{Section::Main, 1, "B"},    StyleSlot{Section::Main, 2, "C"},
    StyleSlot{Section::Main, 3, "D"},    StyleSlot{Section::Fill, 0, "Fill"}, StyleSlot{Section::Break, 0, "Break"},
    StyleSlot{Section::Ending, 0, "E1"}, StyleSlot{Section::Ending, 1, "E2"}, StyleSlot{Section::Ending, 2, "E3"},
};

// What a style file plays, read once from its bytes.
struct StyleSummary {
    std::filesystem::path path;
    std::string name;
    std::string hash;
    double bpm = 120.0;
    core::Meter meter{};
    arranger::Style style;
};

// Nullopt for a file that is missing or no style.
std::optional<StyleSummary> readStyle(const std::filesystem::path& path);
// Every style file in the folders and their subfolders, sorted by name.
std::vector<StyleSummary> listStyles(std::span<const std::filesystem::path> folders);
// True when the style plays the section a Style track block asks for: a fill to a Main needs that Main, and a fill to
// the same Main needs any fill.
bool stylePlays(const arranger::Style& style, Section kind, int variation);
// True when Rhythm 1 plays in a Main. An Audio Style keeps that kit as audio inside the keyboard, so its Rhythm 1 is
// empty.
bool stylePlaysDrums(const arranger::Style& style);
app::Json summaryJson(const StyleSummary& summary);
// What arranger.style.list returns: every style in the Style folders setting.
app::Json styleList(const app::Registry& registry);

// The Arranger device's state on a Style track: the style file, the hash of its bytes and Auto fill.
struct ArrangerState {
    std::string style;
    std::string hash;
    bool autoFill = true;
};
// Nullopt when the track plays no Arranger.
std::optional<ArrangerState> arrangerState(const model::Track& track);
std::vector<std::uint8_t> stateBytes(const ArrangerState& state);

}  // namespace emerick::yamaha
