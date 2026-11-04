#include "emerick/app/plugin_catalog.hpp"

#include <QTemporaryDir>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

#include "emerick/app/nav.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/engine/plugin.hpp"
#include "qt_app.hpp"

using namespace emerick::app;

namespace {

PluginCatalog::Options catalogOptions(const QTemporaryDir& dir, const std::filesystem::path& scanner) {
    return {.cacheFolder = dir.filePath("cache").toStdString(),
            .scanner = scanner,
            .folders = {},
            .timeout = std::chrono::seconds(2)};
}

struct CatalogRig {
    CatalogRig() {
        ensureApplication();
        registerPluginActions(registry, nav, catalog);
        registry.setSetting(kClapFoldersSetting, Json::array({EMERICK_TEST_CLAP_FOLDER}));
    }

    QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    Nav nav;
    PluginCatalog catalog{catalogOptions(dir, EMERICK_PLUGIN_SCANNER)};
};

std::string reasonFor(const Json& blocked, const std::string& name) {
    const auto found = std::ranges::find_if(
        blocked, [&](const Json& entry) { return entry.at("bundle").get<std::string>().contains(name); });
    return found == blocked.end() ? std::string{} : found->at("reason").get<std::string>();
}

}  // namespace

TEST_CASE("listing plugins scans each bundle in a child and blocks the bundles that crash or hang") {
    CatalogRig rig;
    const Json listed = rig.registry.run("plugins.list");
    REQUIRE(listed.at("plugins").size() == 1);
    CHECK(listed.at("plugins")[0].at("id") == "org.emerick.test.sine");
    CHECK(listed.at("plugins")[0].at("instrument") == true);
    CHECK(listed.at("blocked").size() == 2);
    CHECK(reasonFor(listed.at("blocked"), "crash") == "crashed while loading");
    CHECK(reasonFor(listed.at("blocked"), "hang").starts_with("did not load within"));
}

TEST_CASE("listing plugins from the menu shows what it found as a notice") {
    CatalogRig rig;
    QString notice;
    QObject::connect(&rig.registry, &Registry::noticeRaised,
                     [&](const QString& title, const QString& text) { notice = title + ": " + text; });
    rig.registry.activate("plugins.list");
    CHECK(notice == "List plugins: Found 1 CLAP and 0 VST3 plugins, 2 bundles blocked");
}

TEST_CASE("a later scan reads unchanged bundles from the cache and keeps blocked bundles blocked") {
    CatalogRig rig;
    rig.registry.run("plugins.list");
    PluginCatalog cached(catalogOptions(rig.dir, "/no/such/scanner"));
    cached.scan({EMERICK_TEST_CLAP_FOLDER}, false);
    REQUIRE(cached.find("org.emerick.test.sine").has_value());
    CHECK(cached.blocked().size() == 2);
    CHECK_THROWS_AS(cached.scan({EMERICK_TEST_CLAP_FOLDER}, true), AppError);
}

TEST_CASE("the catalog always searches the app's bundled folder first and lists the General MIDI instrument there") {
    QTemporaryDir dir;
    const std::filesystem::path bundled = bundledClapFolder(EMERICK_GM_APP_FOLDER);
    const auto folders = clapSearchFolders(EMERICK_GM_APP_FOLDER, dir.filePath("libraries").toStdString());
    CHECK(folders.front() == bundled);
    CHECK(folders[1] == std::filesystem::path{dir.filePath("libraries").toStdString()} / "plugins" / "clap");
    PluginCatalog::Options options = catalogOptions(dir, EMERICK_PLUGIN_SCANNER);
    options.folders = {bundled};
    PluginCatalog catalog(options);
    catalog.scan({}, false);
    const auto found = catalog.find(kGeneralMidiPluginId);
    REQUIRE(found.has_value());
    CHECK(found->descriptor.name == "General MIDI");
    CHECK(std::ranges::contains(found->descriptor.features, std::string{"instrument"}));
}

TEST_CASE("the catalog lists the expansion voices instrument in the app's bundled folder") {
    QTemporaryDir dir;
    PluginCatalog::Options options = catalogOptions(dir, EMERICK_PLUGIN_SCANNER);
    options.folders = {bundledClapFolder(EMERICK_GM_APP_FOLDER)};
    PluginCatalog catalog(options);
    catalog.scan({}, false);
    const auto found = catalog.find("org.emerick.voices");
    REQUIRE(found.has_value());
    CHECK(found->descriptor.name == "Expansion Voices");
    CHECK(std::ranges::contains(found->descriptor.features, std::string{"instrument"}));
}

TEST_CASE(
    "with bundled plugins only the catalog ignores user folders, refuses a rescan with them, and plugins "
    "outside the bundled folder never load") {
    const std::string sineBundle = CatalogRig{}.registry.run("plugins.list").at("plugins")[0].at("bundle");
    CatalogRig rig;
    rig.registry.setLimits({.bundledPluginsOnly = true});

    CHECK(rig.registry.run("plugins.list").at("plugins").empty());
    CHECK_THROWS_WITH(rig.registry.run("plugins.rescan"),
                      Catch::Matchers::ContainsSubstring("clear the CLAP and VST3 folders"));
    rig.registry.setSetting(kClapFoldersSetting, Json::array());
    CHECK(rig.registry.run("plugins.rescan").at("plugins").empty());

    const std::filesystem::path bundled = bundledClapFolder(EMERICK_GM_APP_FOLDER);
    emerick::engine::allowPluginsOnlyIn(bundled);
    const auto refused =
        emerick::engine::createPlugin(emerick::core::PluginFormat::clap, sineBundle, "org.emerick.test.sine");
    const auto general = emerick::engine::createPlugin(emerick::core::PluginFormat::clap, bundled / "Emerick GM.clap",
                                                       kGeneralMidiPluginId);
    emerick::engine::allowPluginsOnlyIn(std::nullopt);
    CHECK_FALSE(refused.has_value());
    CHECK(refused.error().contains("loads only the plugins the app ships"));
    CHECK(general.has_value());
    CHECK(emerick::engine::createPlugin(emerick::core::PluginFormat::clap, sineBundle, "org.emerick.test.sine")
              .has_value());
}

TEST_CASE("rescanning loads every bundle again") {
    CatalogRig rig;
    rig.registry.run("plugins.list");
    const Json rescanned = rig.registry.run("plugins.rescan");
    CHECK(rescanned.at("plugins").size() == 1);
    CHECK(rescanned.at("blocked").size() == 2);
}
