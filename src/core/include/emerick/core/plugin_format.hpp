#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace emerick::core {

enum class PluginFormat : std::uint8_t { clap, vst3 };

struct PluginFormatName {
    PluginFormat format;
    std::string_view id;
    std::string_view title;
    // The extension of a bundle in this format.
    std::string_view extension;
};

inline constexpr std::array kPluginFormats{
    PluginFormatName{PluginFormat::clap, "clap", "CLAP", ".clap"},
    PluginFormatName{PluginFormat::vst3, "vst3", "VST3", ".vst3"},
};

constexpr const PluginFormatName& pluginFormatName(PluginFormat format) {
    return kPluginFormats.at(static_cast<std::size_t>(format));
}

constexpr std::optional<PluginFormat> pluginFormatFromId(std::string_view id) {
    const auto* const found = std::ranges::find(kPluginFormats, id, &PluginFormatName::id);
    return found == kPluginFormats.end() ? std::nullopt : std::optional{found->format};
}

constexpr std::optional<PluginFormat> pluginFormatOfExtension(std::string_view extension) {
    const auto* const found = std::ranges::find(kPluginFormats, extension, &PluginFormatName::extension);
    return found == kPluginFormats.end() ? std::nullopt : std::optional{found->format};
}

}  // namespace emerick::core
