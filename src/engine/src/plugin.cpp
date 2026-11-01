#include "emerick/engine/plugin.hpp"

namespace emerick::engine {

PluginResult createPlugin(PluginFormat format, const std::filesystem::path& bundle, const std::string& pluginId) {
    switch (format) {
        case PluginFormat::clap:
            return createClapPlugin(bundle, pluginId);
        case PluginFormat::vst3:
            return createVst3Plugin(bundle, pluginId);
    }
    return std::unexpected("unknown plugin format");
}

}  // namespace emerick::engine
