#include "emerick/engine/plugin_scan.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>
#include <string_view>

#include "clap_library.hpp"

namespace emerick::engine {

namespace {

struct FormatFeatures {
    std::string_view instrument;
    std::string_view effect;
};

// Indexed by PluginFormat.
constexpr std::array kFormatFeatures{FormatFeatures{.instrument = "instrument", .effect = "audio-effect"},
                                     FormatFeatures{.instrument = "Instrument", .effect = "Fx"}};

const FormatFeatures& featuresOf(core::PluginFormat format) {
    return kFormatFeatures.at(static_cast<std::size_t>(format));
}

std::string text(const char* value) { return value != nullptr ? value : ""; }

std::vector<std::string> features(const char* const* list) {
    std::vector<std::string> names;
    for (; list != nullptr && *list != nullptr; ++list) {
        names.emplace_back(*list);
    }
    return names;
}

}  // namespace

bool isInstrument(const PluginDescriptor& plugin) {
    return std::ranges::contains(plugin.features, featuresOf(plugin.format).instrument);
}

bool isEffect(const PluginDescriptor& plugin) {
    return std::ranges::contains(plugin.features, featuresOf(plugin.format).effect);
}

std::expected<std::vector<PluginDescriptor>, std::string> describeBundle(const std::filesystem::path& bundle) {
    const auto format = core::pluginFormatOfExtension(bundle.extension().string());
    if (!format) {
        return std::unexpected(bundle.string() + " is no CLAP or VST3 bundle");
    }
    switch (*format) {
        case core::PluginFormat::clap:
            return describeClapBundle(bundle);
        case core::PluginFormat::vst3:
            return describeVst3Bundle(bundle);
    }
    return std::unexpected(bundle.string() + " is no CLAP or VST3 bundle");
}

std::expected<std::vector<PluginDescriptor>, std::string> describeClapBundle(const std::filesystem::path& bundle) {
    return ClapLibrary::open(bundle).transform([](const std::shared_ptr<ClapLibrary>& library) {
        const clap_plugin_factory& factory = library->factory();
        return std::views::iota(std::uint32_t{0}, factory.get_plugin_count(&factory)) |
               std::views::transform(
                   [&](std::uint32_t index) { return factory.get_plugin_descriptor(&factory, index); }) |
               std::views::filter([](const clap_plugin_descriptor* found) { return found != nullptr; }) |
               std::views::transform([](const clap_plugin_descriptor* found) {
                   return PluginDescriptor{.format = core::PluginFormat::clap,
                                           .id = text(found->id),
                                           .name = text(found->name),
                                           .vendor = text(found->vendor),
                                           .version = text(found->version),
                                           .description = text(found->description),
                                           .features = features(found->features)};
               }) |
               std::ranges::to<std::vector>();
    });
}

}  // namespace emerick::engine
