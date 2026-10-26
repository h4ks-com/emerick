#include "emerick/engine/clap_scan.hpp"

#include <cstdint>
#include <ranges>

#include "clap_library.hpp"

namespace emerick::engine {

namespace {

std::string text(const char* value) { return value != nullptr ? value : ""; }

std::vector<std::string> features(const char* const* list) {
    std::vector<std::string> names;
    for (; list != nullptr && *list != nullptr; ++list) {
        names.emplace_back(*list);
    }
    return names;
}

}  // namespace

std::expected<std::vector<ClapDescriptor>, std::string> describeClapBundle(const std::filesystem::path& bundle) {
    return ClapLibrary::open(bundle).transform([](const std::shared_ptr<ClapLibrary>& library) {
        const clap_plugin_factory& factory = library->factory();
        return std::views::iota(std::uint32_t{0}, factory.get_plugin_count(&factory)) |
               std::views::transform(
                   [&](std::uint32_t index) { return factory.get_plugin_descriptor(&factory, index); }) |
               std::views::filter([](const clap_plugin_descriptor* found) { return found != nullptr; }) |
               std::views::transform([](const clap_plugin_descriptor* found) {
                   return ClapDescriptor{.id = text(found->id),
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
