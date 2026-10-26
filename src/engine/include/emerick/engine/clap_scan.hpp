#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace emerick::engine {

// What a CLAP bundle says about one of its plugins. Features are CLAP's feature names, such as "instrument".
struct ClapDescriptor {
    std::string id;
    std::string name;
    std::string vendor;
    std::string version;
    std::string description;
    std::vector<std::string> features;

    bool operator==(const ClapDescriptor&) const = default;
};

// Loads the bundle in this process, so the app runs it in a scanner child to survive a plugin that crashes.
std::expected<std::vector<ClapDescriptor>, std::string> describeClapBundle(const std::filesystem::path& bundle);

}  // namespace emerick::engine
