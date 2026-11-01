#include "emerick/engine/plugin.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace emerick::engine {

namespace {

std::optional<std::filesystem::path>& pluginFolder() {
    static std::optional<std::filesystem::path> folder;
    return folder;
}

bool insideFolder(const std::filesystem::path& bundle, const std::filesystem::path& folder) {
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(bundle, error);
    return !error && std::ranges::mismatch(folder, resolved).in1 == folder.end();
}

}  // namespace

void allowPluginsOnlyIn(std::optional<std::filesystem::path> folder) {
    if (folder) {
        std::error_code error;
        if (auto canonical = std::filesystem::weakly_canonical(*folder, error); !error) {
            folder = std::move(canonical);
        }
    }
    pluginFolder() = std::move(folder);
}

PluginResult createPlugin(PluginFormat format, const std::filesystem::path& bundle, const std::string& pluginId) {
    if (const auto& folder = pluginFolder(); folder && !insideFolder(bundle, *folder)) {
        return std::unexpected(bundle.string() + " is outside " + folder->string() +
                               ", and this run loads only the plugins the app ships");
    }
    switch (format) {
        case PluginFormat::clap:
            return createClapPlugin(bundle, pluginId);
        case PluginFormat::vst3:
            return createVst3Plugin(bundle, pluginId);
    }
    return std::unexpected("unknown plugin format");
}

}  // namespace emerick::engine
