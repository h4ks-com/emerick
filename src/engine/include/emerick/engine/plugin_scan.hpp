#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "emerick/core/plugin_format.hpp"

namespace emerick::engine {

// What a bundle says about one of its plugins. Features are CLAP's feature names, such as "instrument", or VST3's
// subcategories, such as "Fx".
struct PluginDescriptor {
    core::PluginFormat format = core::PluginFormat::clap;
    std::string id;
    std::string name;
    std::string vendor;
    std::string version;
    std::string description;
    std::vector<std::string> features;

    bool operator==(const PluginDescriptor&) const = default;
};

[[nodiscard]] bool isInstrument(const PluginDescriptor& plugin);
[[nodiscard]] bool isEffect(const PluginDescriptor& plugin);

// Loads a CLAP or VST3 bundle in this process, so the app runs it in a scanner child to survive a plugin that crashes.
// A VST3 bundle with a moduleinfo.json is described from that file without loading.
std::expected<std::vector<PluginDescriptor>, std::string> describeBundle(const std::filesystem::path& bundle);

std::expected<std::vector<PluginDescriptor>, std::string> describeClapBundle(const std::filesystem::path& bundle);
std::expected<std::vector<PluginDescriptor>, std::string> describeVst3Bundle(const std::filesystem::path& bundle);

}  // namespace emerick::engine
