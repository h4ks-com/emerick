#include "emerick/app/credits.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "emerick/app/libraries.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

struct CreditsRig : ProjectRig {
    CreditsRig() {
        registerPluginActions(registry, nav, catalog);
        registerInstrumentActions(registry, *controller, nav, catalog, path("libraries"));
        registry.setSetting(kClapFoldersSetting, Json::array({bundledClapFolder(EMERICK_GM_APP_FOLDER).string()}));
        libraries = registerLibraries(registry, nav, path("libraries"));
        registerCreditActions(registry, *controller, *libraries);
    }

    PluginCatalog catalog{{.cacheFolder = path("cache"), .scanner = EMERICK_PLUGIN_SCANNER, .folders = {}}};
    std::unique_ptr<Libraries> libraries;
};

}  // namespace

TEST_CASE("project.credits credits the library program and the DX7 cartridge the project plays") {
    CreditsRig rig;
    if (!std::filesystem::exists(bundledClapFolder(EMERICK_GM_APP_FOLDER) / "Dexed.clap")) {
        SKIP("Dexed does not ship on this platform");
    }
    const std::filesystem::path library = rig.path("libraries") / "vsco2-ce";
    std::filesystem::create_directories(library / "Strings");
    std::filesystem::copy(EMERICK_SAMPLER_SINE, library / "Strings", std::filesystem::copy_options::recursive);
    rig.run("sampler.load",
            {{"track", rig.run("track.addMidi").at("id")}, {"library", "vsco2-ce"}, {"file", "Strings/sine.sfz"}});
    rig.run("dexed.load",
            {{"track", rig.run("track.addMidi").at("id")}, {"cartridge", "synprezfm_01"}, {"program", 2}});

    const Json credits = rig.run("project.credits");
    REQUIRE(credits.at("libraries").size() == 1);
    CHECK(credits.at("libraries")[0].at("id") == "vsco2-ce");
    CHECK(credits.at("libraries")[0].at("license") == "CC0-1.0");
    CHECK(credits.at("libraries")[0].at("programs") == Json{"Strings/sine.sfz"});
    REQUIRE(credits.at("cartridges").size() == 1);
    CHECK(credits.at("cartridges")[0].at("id") == "synprezfm_01");
    CHECK(credits.at("cartridges")[0].at("license") == "GPL-3.0");
    CHECK(credits.at("cartridges")[0].at("programs") == Json{"HARPSICH 1"});
    CHECK(std::ranges::contains(credits.at("plugins"), Json(kDexedPluginId),
                                [](const Json& plugin) { return plugin.at("id"); }));
}

TEST_CASE("catalog.export writes the libraries, plugins and cartridges as JSON") {
    CreditsRig rig;
    const std::string file = rig.path("catalog.json").string();
    CHECK(rig.run("catalog.export", {{"path", file}}).at("file") == file);
    const Json exported = Json::parse(std::ifstream(file));
    CHECK(exported.at("libraries").size() == curatedLibraries().size());
    CHECK(exported.at("libraries")[0].at("programs")[0].at("gmSounds")[0] == "Acoustic Grand Piano");
    CHECK(exported.at("soundFont").at("name") == "GeneralUser GS v2.0.3");
    CHECK(exported.at("cartridges").size() > 0);
    CHECK(std::ranges::contains(exported.at("plugins"), Json("org.emerick.sampler"),
                                [](const Json& plugin) { return plugin.at("id"); }));
    CHECK(rig.run("catalog.export") == exported);
}
