#include "emerick/app/plugin_catalog.hpp"

#include <QDir>
#include <QProcess>
#include <QtEnvironmentVariables>
#include <QtSystemDetection>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <ranges>
#include <system_error>
#include <utility>

#include "emerick/app/registry.hpp"

namespace emerick::app {

namespace {

constexpr const char* kCacheFile = "plugins.json";

long long modifiedTime(const std::filesystem::path& path) {
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    return error ? 0 : static_cast<long long>(time.time_since_epoch().count());
}

// A bundle is a .clap or .vst3 file or folder; we never look inside a bundle for more.
std::vector<std::filesystem::path> findBundles(const std::vector<std::filesystem::path>& folders) {
    std::vector<std::filesystem::path> bundles;
    for (const std::filesystem::path& folder : folders) {
        std::error_code error;
        std::filesystem::recursive_directory_iterator entries(
            folder, std::filesystem::directory_options::skip_permission_denied, error);
        for (; !error && entries != std::filesystem::recursive_directory_iterator(); entries.increment(error)) {
            if (core::pluginFormatOfExtension(entries->path().extension().string())) {
                bundles.push_back(entries->path());
                entries.disable_recursion_pending();
            }
        }
    }
    std::ranges::sort(bundles);
    const auto [first, last] = std::ranges::unique(bundles);
    bundles.erase(first, last);
    return bundles;
}

Json descriptorJson(const engine::PluginDescriptor& plugin) {
    return {{"format", core::pluginFormatName(plugin.format).id},
            {"id", plugin.id},
            {"name", plugin.name},
            {"vendor", plugin.vendor},
            {"version", plugin.version},
            {"description", plugin.description},
            {"features", plugin.features}};
}

engine::PluginDescriptor descriptorFromJson(const Json& json) {
    const auto format = core::pluginFormatFromId(json.value("format", "clap"));
    if (!format) {
        throw Json::other_error::create(0, "unknown plugin format " + json.at("format").dump(), &json);
    }
    return {.format = *format,
            .id = json.at("id"),
            .name = json.at("name"),
            .vendor = json.value("vendor", ""),
            .version = json.value("version", ""),
            .description = json.value("description", ""),
            .features = json.value("features", std::vector<std::string>{})};
}

std::vector<engine::PluginDescriptor> descriptorsFromJson(const Json& json) {
    return json | std::views::transform(descriptorFromJson) | std::ranges::to<std::vector>();
}

}  // namespace

PluginCatalog::PluginCatalog(Options options) : options_(std::move(options)) { load(); }

void PluginCatalog::scan(const std::vector<std::filesystem::path>& userFolders, bool full) {
    if (full) {
        bundles_.clear();
    }
    std::vector<std::filesystem::path> folders = options_.folders;
    folders.insert(folders.end(), userFolders.begin(), userFolders.end());
    std::map<std::filesystem::path, Bundle> found;
    for (const std::filesystem::path& path : findBundles(folders)) {
        const long long modified = modifiedTime(path);
        auto known = bundles_.find(path);
        if (known != bundles_.end() && known->second.modified == modified) {
            found.insert(bundles_.extract(known));
        } else {
            found.emplace(path, runScanner(path, modified));
        }
    }
    bundles_ = std::move(found);
    scanned_ = true;
    save();
}

std::vector<CatalogPlugin> PluginCatalog::plugins() const {
    std::vector<CatalogPlugin> listed;
    for (const auto& [path, bundle] : bundles_) {
        for (const engine::PluginDescriptor& plugin : bundle.plugins) {
            listed.push_back({.descriptor = plugin, .bundle = path});
        }
    }
    return listed;
}

std::optional<CatalogPlugin> PluginCatalog::find(const std::string& id,
                                                 std::optional<core::PluginFormat> format) const {
    const std::vector<CatalogPlugin> listed = plugins();
    const auto found = std::ranges::find_if(listed, [&](const CatalogPlugin& plugin) {
        return plugin.descriptor.id == id && (!format || plugin.descriptor.format == *format);
    });
    return found == listed.end() ? std::nullopt : std::optional{*found};
}

std::map<std::filesystem::path, std::string> PluginCatalog::blocked() const {
    return bundles_ | std::views::filter([](const auto& entry) { return !entry.second.blockedReason.empty(); }) |
           std::views::transform([](const auto& entry) { return std::pair{entry.first, entry.second.blockedReason}; }) |
           std::ranges::to<std::map>();
}

PluginCatalog::Bundle PluginCatalog::runScanner(const std::filesystem::path& bundle, long long modified) const {
    QProcess process;
    process.start(QString::fromStdString(options_.scanner.string()), {QString::fromStdString(bundle.string())});
    if (!process.waitForStarted()) {
        throw AppError("cannot run the plugin scanner " + options_.scanner.string() + ": " +
                       process.errorString().toStdString());
    }
    Bundle result{.modified = modified, .plugins = {}, .blockedReason = {}};
    if (!process.waitForFinished(static_cast<int>(options_.timeout.count()))) {
        process.kill();
        process.waitForFinished();
        result.blockedReason = "did not load within " + std::to_string(options_.timeout.count()) + " ms";
        return result;
    }
    if (process.exitStatus() == QProcess::CrashExit) {
        result.blockedReason = "crashed while loading";
        return result;
    }
    if (process.exitCode() != 0) {
        result.blockedReason = process.readAllStandardError().trimmed().toStdString();
        return result;
    }
    try {
        // A plugin may print to stdout while it loads, so we read the list from the scanner's last line.
        const QList<QByteArray> lines = process.readAllStandardOutput().trimmed().split('\n');
        result.plugins = descriptorsFromJson(Json::parse(lines.back().toStdString()).at("plugins"));
    } catch (const Json::exception& error) {
        result.blockedReason = std::string{"printed no plugin list: "} + error.what();
    }
    return result;
}

void PluginCatalog::load() {
    std::ifstream in(options_.cacheFolder / kCacheFile);
    if (!in) {
        return;
    }
    try {
        const Json cache = Json::parse(in);
        for (const auto& [path, entry] : cache.at("bundles").items()) {
            bundles_[path] = {.modified = entry.at("modified"),
                              .plugins = descriptorsFromJson(entry.at("plugins")),
                              .blockedReason = entry.value("blocked", "")};
        }
    } catch (const Json::exception& error) {
        qWarning("ignoring the plugin cache: %s", error.what());
        bundles_.clear();
    }
}

void PluginCatalog::save() const {
    Json bundles = Json::object();
    for (const auto& [path, bundle] : bundles_) {
        bundles[path.string()] = {
            {"modified", bundle.modified},
            {"plugins", bundle.plugins | std::views::transform(descriptorJson) | std::ranges::to<Json::array_t>()},
            {"blocked", bundle.blockedReason}};
    }
    std::error_code error;
    std::filesystem::create_directories(options_.cacheFolder, error);
    std::ofstream out(options_.cacheFolder / kCacheFile);
    out << Json{{"bundles", bundles}}.dump(2) << '\n';
    if (!out) {
        qWarning("cannot write the plugin cache in %s", options_.cacheFolder.string().c_str());
    }
}

std::filesystem::path bundledClapFolder(const std::filesystem::path& appFolder) {
#ifdef Q_OS_MACOS
    return appFolder.parent_path() / "PlugIns";
#else
    return appFolder / "clap";
#endif
}

std::vector<std::filesystem::path> clapSearchFolders(const std::filesystem::path& appFolder) {
    const std::filesystem::path home = QDir::homePath().toStdString();
#ifdef Q_OS_MACOS
    std::vector<std::filesystem::path> folders{bundledClapFolder(appFolder), home / "Library/Audio/Plug-Ins/CLAP",
                                               "/Library/Audio/Plug-Ins/CLAP"};
#elifdef Q_OS_WIN
    std::vector<std::filesystem::path> folders{
        bundledClapFolder(appFolder),
        std::filesystem::path{qEnvironmentVariable("LOCALAPPDATA").toStdWString()} / "Programs/Common/CLAP",
        std::filesystem::path{qEnvironmentVariable("COMMONPROGRAMFILES").toStdWString()} / "CLAP"};
#else
    std::vector<std::filesystem::path> folders{bundledClapFolder(appFolder), home / ".clap", "/usr/lib/clap"};
#endif
    for (const QString& folder : qEnvironmentVariable("CLAP_PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
        folders.emplace_back(folder.toStdString());
    }
    return folders;
}

std::vector<std::filesystem::path> vst3SearchFolders() {
    const std::filesystem::path home = QDir::homePath().toStdString();
#ifdef Q_OS_MACOS
    return {home / "Library/Audio/Plug-Ins/VST3", "/Library/Audio/Plug-Ins/VST3"};
#elifdef Q_OS_WIN
    return {std::filesystem::path{qEnvironmentVariable("LOCALAPPDATA").toStdWString()} / "Programs/Common/VST3",
            std::filesystem::path{qEnvironmentVariable("COMMONPROGRAMFILES").toStdWString()} / "VST3"};
#else
    return {home / ".vst3", "/usr/lib/vst3", "/usr/local/lib/vst3"};
#endif
}

std::vector<std::filesystem::path> pluginSearchFolders(const std::filesystem::path& appFolder) {
    std::vector<std::filesystem::path> folders = clapSearchFolders(appFolder);
    std::ranges::copy(vst3SearchFolders(), std::back_inserter(folders));
    return folders;
}

}  // namespace emerick::app
