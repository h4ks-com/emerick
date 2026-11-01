#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "emerick/engine/plugin_scan.hpp"

namespace emerick::app {

struct CatalogPlugin {
    engine::PluginDescriptor descriptor;
    std::filesystem::path bundle;
};

// The CLAP plugins installed on this machine. Each bundle loads in a scanner child process, so a plugin that crashes
// or hangs only lands on the blocklist. We keep what each bundle held, keyed by its path and modification time, in
// clap-plugins.json in the cache folder, so a later scan only runs the scanner for new or changed bundles.
class PluginCatalog {
public:
    struct Options {
        std::filesystem::path cacheFolder;
        std::filesystem::path scanner;
        // The folders every scan searches, before the user's own.
        std::vector<std::filesystem::path> folders;
        std::chrono::milliseconds timeout = std::chrono::seconds(10);
    };

    explicit PluginCatalog(Options options);

    // Scans every bundle in the catalog's folders and userFolders, skipping blocked bundles until they change; a full
    // scan forgets the cache and the blocklist first. The scan runs on the calling thread and waits for each scanner
    // child in turn.
    void scan(const std::vector<std::filesystem::path>& userFolders, bool full);
    [[nodiscard]] bool scanned() const { return scanned_; }
    [[nodiscard]] std::vector<CatalogPlugin> plugins() const;
    [[nodiscard]] std::optional<CatalogPlugin> find(const std::string& id) const;
    // Each blocked bundle with the reason, such as that it crashed while loading.
    [[nodiscard]] std::map<std::filesystem::path, std::string> blocked() const;

private:
    struct Bundle {
        long long modified = 0;
        std::vector<engine::PluginDescriptor> plugins;
        // Set for a bundle that crashed, hung or failed to load.
        std::string blockedReason;
    };

    Bundle runScanner(const std::filesystem::path& bundle, long long modified) const;
    void load();
    void save() const;

    Options options_;
    std::map<std::filesystem::path, Bundle> bundles_;
    bool scanned_ = false;
};

// The folder of CLAP bundles that ship with the app whose executable sits in appFolder.
std::filesystem::path bundledClapFolder(const std::filesystem::path& appFolder);

// The app's bundled CLAP folder, the standard CLAP folders of this platform, then each folder in CLAP_PATH.
std::vector<std::filesystem::path> clapSearchFolders(const std::filesystem::path& appFolder);

}  // namespace emerick::app
