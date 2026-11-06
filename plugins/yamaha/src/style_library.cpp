#include "style_library.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <system_error>

#include "emerick/core/base64.hpp"

namespace emerick::yamaha {

namespace {

std::string lowercase(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char letter) { return std::tolower(letter); });
    return text;
}

bool isStyleFile(const std::filesystem::path& path) {
    const std::string extension = lowercase(path.extension().string());
    return extension.size() > 1 && std::ranges::contains(kStyleExtensions, std::string_view{extension}.substr(1));
}

}  // namespace

std::optional<std::vector<std::uint8_t>> readStyleBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::optional<StyleSummary> readStyle(const std::filesystem::path& path) {
    const auto bytes = readStyleBytes(path);
    return bytes ? parseStyleSummary(path, *bytes) : std::nullopt;
}

std::optional<StyleSummary> stateStyle(const ArrangerState& state) {
    if (!state.data.empty()) {
        return parseStyleSummary(state.style, state.data);
    }
    return state.style.empty() ? std::nullopt : readStyle(state.style);
}

std::optional<StyleSummary> parseStyleSummary(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    try {
        arranger::Style style = arranger::parseStyle(bytes);
        return StyleSummary{.path = path,
                            .name = path.stem().string(),
                            .hash = arranger::styleHash(bytes),
                            .bpm = style.bpm,
                            .meter = style.meter,
                            .style = std::move(style)};
    } catch (const arranger::StyleError&) {
        return std::nullopt;
    }
}

std::vector<StyleSummary> listStyles(std::span<const std::filesystem::path> folders) {
    std::vector<StyleSummary> styles;
    for (const std::filesystem::path& folder : folders) {
        std::error_code error;
        for (auto entry = std::filesystem::recursive_directory_iterator(
                 folder, std::filesystem::directory_options::skip_permission_denied, error);
             !error && entry != std::filesystem::recursive_directory_iterator(); entry.increment(error)) {
            if (entry->is_regular_file(error) && isStyleFile(entry->path())) {
                if (auto summary = readStyle(entry->path())) {
                    styles.push_back(*std::move(summary));
                }
            }
        }
    }
    std::ranges::sort(styles, {}, [](const StyleSummary& summary) { return lowercase(summary.name); });
    return styles;
}

bool stylePlays(const arranger::Style& style, Section kind, int variation) {
    switch (kind) {
        case Section::Main:
        case Section::Break:
        case Section::Intro:
        case Section::Ending:
            return arranger::findSection(style, kind, variation) != nullptr;
        case Section::FillTo:
            return arranger::findSection(style, Section::Main, variation) != nullptr;
        case Section::Fill:
            return std::ranges::contains(style.sections, Section::FillTo, &arranger::StyleSection::kind);
    }
    return false;
}

bool stylePlaysDrums(const arranger::Style& style) {
    return std::ranges::any_of(style.sections, [](const arranger::StyleSection& section) {
        return section.kind == Section::Main && !arranger::partNotes(section).front().empty();
    });
}

app::Json summaryJson(const StyleSummary& summary) {
    return {{"path", summary.path.string()},
            {"name", summary.name},
            {"bpm", summary.bpm},
            {"meter", std::to_string(summary.meter.numerator) + "/" + std::to_string(summary.meter.denominator)},
            {"sections", kStyleSlots | std::views::filter([&](const StyleSlot& slot) {
                             return stylePlays(summary.style, slot.kind, slot.variation);
                         }) | std::views::transform([](const StyleSlot& slot) { return std::string{slot.label}; }) |
                             std::ranges::to<std::vector>()}};
}

app::Json styleList(std::span<const StyleSummary> summaries) {
    const app::Json styles = summaries | std::views::transform(summaryJson) | std::ranges::to<std::vector>();
    return {{"styles", styles}, {"message", std::to_string(styles.size()) + " styles"}};
}

app::Json styleList(const app::Registry& registry) {
    const auto folders = registry.settingValue(kStyleFoldersSetting).get<std::vector<std::string>>() |
                         std::ranges::to<std::vector<std::filesystem::path>>();
    return styleList(listStyles(folders));
}

std::optional<ArrangerState> arrangerState(const model::Track& track) {
    if (!track.instrument || track.instrument->pluginId != kArrangerPluginId) {
        return std::nullopt;
    }
    const std::vector<std::uint8_t>& state = track.instrument->state;
    return parseArrangerState(std::string{state.begin(), state.end()});
}

std::vector<std::uint8_t> stateBytes(const ArrangerState& state) {
    app::Json json{{"style", state.style}, {"hash", state.hash}, {"autoFill", state.autoFill}};
    if (!state.data.empty()) {
        json["data"] = core::toBase64(state.data);
    }
    const std::string text = json.dump();
    return {text.begin(), text.end()};
}

}  // namespace emerick::yamaha
