// Loads one CLAP bundle and prints its plugins as JSON, so a plugin that crashes takes only this process down.

#include <cstdio>
#include <exception>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <print>
#include <ranges>
#include <span>

#include "emerick/engine/clap_scan.hpp"

namespace {

int scan(const std::filesystem::path& bundle) {
    const auto described = emerick::engine::describeClapBundle(bundle);
    if (!described) {
        std::println(stderr, "{}", described.error());
        return 1;
    }
    const nlohmann::json plugins = *described |
                                   std::views::transform([](const emerick::engine::ClapDescriptor& plugin) {
                                       return nlohmann::json{{"id", plugin.id},
                                                             {"name", plugin.name},
                                                             {"vendor", plugin.vendor},
                                                             {"version", plugin.version},
                                                             {"description", plugin.description},
                                                             {"features", plugin.features}};
                                   }) |
                                   std::ranges::to<nlohmann::json::array_t>();
    std::println("{}", nlohmann::json{{"plugins", plugins}}.dump());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::span arguments(argv, static_cast<std::size_t>(argc));
    if (arguments.size() != 2) {
        std::fputs("usage: emerick-clap-scanner <bundle.clap>\n", stderr);
        return 2;
    }
    try {
        return scan(arguments[1]);
    } catch (const std::exception& error) {
        std::fputs(error.what(), stderr);
        return 1;
    }
}
