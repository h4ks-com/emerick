#include <public.sdk/source/vst/hosting/module.h>
#include <public.sdk/source/vst/moduleinfo/moduleinfoparser.h>

#include <fstream>
#include <iterator>
#include <ranges>
#include <sstream>

#include "emerick/engine/plugin_scan.hpp"

namespace emerick::engine {

namespace {

// The category of the classes that process audio, as opposed to their controllers.
constexpr std::string_view kAudioModuleClass = "Audio Module Class";

PluginDescriptor descriptor(const VST3::UID& id, std::string name, std::string vendor, std::string version,
                            std::vector<std::string> subCategories) {
    return {.format = core::PluginFormat::vst3,
            .id = id.toString(false),
            .name = std::move(name),
            .vendor = std::move(vendor),
            .version = std::move(version),
            .description = {},
            .features = std::move(subCategories)};
}

std::expected<std::vector<PluginDescriptor>, std::string> fromModuleInfo(const std::string& path) {
    std::ifstream in(path);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::ostringstream errors;
    const auto info = Steinberg::ModuleInfoLib::parseJson(text, &errors);
    if (!info) {
        return std::unexpected(path + " is no module info: " + errors.str());
    }
    std::vector<PluginDescriptor> plugins;
    for (const Steinberg::ModuleInfo::ClassInfo& described : info->classes) {
        const auto id = VST3::UID::fromString(described.cid);
        if (described.category != kAudioModuleClass || !id) {
            continue;
        }
        plugins.push_back(descriptor(*id, described.name,
                                     described.vendor.empty() ? info->factoryInfo.vendor : described.vendor,
                                     described.version, described.subCategories));
    }
    return plugins;
}

}  // namespace

std::expected<std::vector<PluginDescriptor>, std::string> describeVst3Bundle(const std::filesystem::path& bundle) {
    if (const auto moduleInfo = VST3::Hosting::Module::getModuleInfoPath(bundle.string())) {
        return fromModuleInfo(*moduleInfo);
    }
    std::string error;
    const auto module = VST3::Hosting::Module::create(bundle.string(), error);
    if (!module) {
        return std::unexpected("cannot load " + bundle.string() + ": " + error);
    }
    const VST3::Hosting::PluginFactory& factory = module->getFactory();
    const std::string factoryVendor = factory.info().vendor();
    return factory.classInfos() | std::views::filter([](const VST3::Hosting::ClassInfo& described) {
               return described.category() == kAudioModuleClass;
           }) |
           std::views::transform([&](const VST3::Hosting::ClassInfo& described) {
               return descriptor(described.ID(), described.name(),
                                 described.vendor().empty() ? factoryVendor : described.vendor(), described.version(),
                                 described.subCategories());
           }) |
           std::ranges::to<std::vector>();
}

}  // namespace emerick::engine
