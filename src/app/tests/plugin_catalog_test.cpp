#include "emerick/app/plugin_catalog.hpp"

#include <QTemporaryDir>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <string>

#include "emerick/app/nav.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/registry.hpp"
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
    PluginCatalog catalog{catalogOptions(dir, EMERICK_CLAP_SCANNER)};
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
    CHECK(notice == "List plugins: Found 1 CLAP plugins, 2 bundles blocked");
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

TEST_CASE("rescanning loads every bundle again") {
    CatalogRig rig;
    rig.registry.run("plugins.list");
    const Json rescanned = rig.registry.run("plugins.rescan");
    CHECK(rescanned.at("plugins").size() == 1);
    CHECK(rescanned.at("blocked").size() == 2);
}
