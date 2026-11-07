#include "emerick/app/libraries.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <QtEnvironmentVariables>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/nav.hpp"
#include "emerick/app/registry.hpp"
#include "qt_app.hpp"

using namespace emerick::app;
using Catch::Matchers::ContainsSubstring;

namespace {

std::vector<std::string> entries(const std::filesystem::path& folder) {
    std::vector<std::string> names;
    if (std::filesystem::exists(folder)) {
        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            names.push_back(entry.path().filename().string());
        }
    }
    return names;
}

struct LibraryRig {
    explicit LibraryRig(const std::string& sha256 = {}, const std::string& url = {}) {
        ensureApplication();
        const std::filesystem::path source = path("source/fixture-1.0");
        std::filesystem::create_directories(source / "Samples");
        std::ofstream(source / "Piano.sfz") << "<region> sample=Samples/a.wav\n";
        std::ofstream(source / "Samples" / "a.wav") << "not really audio";
        REQUIRE(QProcess::execute("tar", {"-czf", archive(), "-C", qs(path("source").string()), "fixture-1.0"}) == 0);
        QFile file(archive());
        REQUIRE(file.open(QIODevice::ReadOnly));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&file);
        libraries =
            registerLibraries(registry, nav, folder(),
                              {{.id = "fixture",
                                .name = "Fixture Piano",
                                .description = "A piano for tests",
                                .url = url.empty() ? QUrl::fromLocalFile(archive()).toString().toStdString() : url,
                                .sha256 = sha256.empty() ? hash.result().toHex().toStdString() : sha256,
                                .size = file.size(),
                                .license = "CC0-1.0",
                                .sfz = "Piano.sfz"}});
    }

    [[nodiscard]] std::filesystem::path path(const std::string& name) const {
        return std::filesystem::path{dir.path().toStdString()} / name;
    }
    [[nodiscard]] std::filesystem::path folder() const { return path("libraries"); }
    [[nodiscard]] QString archive() const { return qs(path("fixture.tar.gz").string()); }
    [[nodiscard]] Json listed() const { return registry.run("library.list").at("libraries").at(0); }

    QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    Nav nav;
    std::unique_ptr<Libraries> libraries;
};

}  // namespace

TEST_CASE("installing a library downloads it, checks its hash and extracts it into its own folder") {
    LibraryRig rig;
    CHECK(rig.listed().at("state") == "available");
    const Json installed = rig.registry.run("library.install", {{"library", "fixture"}, {"wait", true}});
    CHECK(installed.at("installed") == true);
    CHECK(installed.at("sfz") == (rig.folder() / "fixture" / "Piano.sfz").string());
    CHECK(std::filesystem::exists(rig.folder() / "fixture" / "Samples" / "a.wav"));
    CHECK(entries(rig.folder()) == std::vector<std::string>{"fixture"});
    CHECK(rig.listed().at("state") == "installed");
    CHECK(rig.listed().at("path") == (rig.folder() / "fixture").string());
}

TEST_CASE("installing an installed library leaves it as it is") {
    LibraryRig rig;
    rig.registry.run("library.install", {{"library", "fixture"}, {"wait", true}});
    CHECK(rig.registry.run("library.install", {{"library", "fixture"}}).at("state") == "installed");
}

TEST_CASE("a download whose sha256 differs is rejected and leaves no folder behind") {
    LibraryRig rig(std::string(64, '0'));
    CHECK_THROWS_WITH(rig.registry.run("library.install", {{"library", "fixture"}, {"wait", true}}),
                      ContainsSubstring("does not match its sha256"));
    CHECK(entries(rig.folder()).empty());
    CHECK_THAT(rig.listed().at("error").get<std::string>(), ContainsSubstring("sha256"));
}

TEST_CASE("a download that fails leaves no folder behind") {
    QTemporaryDir missing;
    LibraryRig rig({}, QUrl::fromLocalFile(missing.filePath("gone.tar.gz")).toString().toStdString());
    CHECK_THROWS_WITH(rig.registry.run("library.install", {{"library", "fixture"}, {"wait", true}}),
                      ContainsSubstring("cannot download Fixture Piano"));
    CHECK(entries(rig.folder()).empty());
    CHECK(rig.listed().at("installed") == false);
}

TEST_CASE("cancelling a download leaves no folder behind") {
    LibraryRig rig;
    CHECK(rig.registry.run("library.install", {{"library", "fixture"}}).at("state") == "downloading");
    rig.registry.run("library.cancel", {{"library", "fixture"}});
    REQUIRE(processUntil([&] { return !rig.libraries->busy("fixture"); }));
    CHECK(entries(rig.folder()).empty());
    CHECK_THAT(rig.listed().at("error").get<std::string>(), ContainsSubstring("cancelled"));
}

TEST_CASE("removing a library asks for confirm and deletes its folder") {
    LibraryRig rig;
    rig.registry.run("library.install", {{"library", "fixture"}, {"wait", true}});
    CHECK_THROWS_AS(rig.registry.run("library.remove", {{"library", "fixture"}}), AppError);
    CHECK(rig.registry.run("library.remove", {{"library", "fixture"}, {"confirm", true}}).at("state") == "available");
    CHECK(entries(rig.folder()).empty());
    CHECK_THROWS_WITH(rig.registry.run("library.remove", {{"library", "fixture"}, {"confirm", true}}),
                      ContainsSubstring("not installed"));
}

TEST_CASE("an unknown library names the ones there are") {
    LibraryRig rig;
    CHECK_THROWS_WITH(rig.registry.run("library.install", {{"library", "harp"}}), ContainsSubstring("fixture"));
}

TEST_CASE("a service run refuses to install or remove libraries and lists the limit") {
    LibraryRig rig;
    rig.registry.setLimits({.libraryInstall = false});
    CHECK_THROWS_WITH(rig.registry.run("library.install", {{"library", "fixture"}}),
                      ContainsSubstring("EMERICK_LIBRARY_INSTALL=1"));
    CHECK_THROWS_AS(rig.registry.run("library.remove", {{"library", "fixture"}, {"confirm", true}}), AppError);
    CHECK(rig.registry.limitsState().at("libraryInstall") == false);
    CHECK(rig.listed().at("installed") == false);
}

TEST_CASE("opening the libraries dialog at a library shows that library") {
    LibraryRig rig;
    rig.registry.run("library.open", {{"library", "fixture"}});
    CHECK(rig.nav.librariesOpen());
    CHECK(rig.nav.shownLibrary() == "fixture");
}

TEST_CASE("a profile run keeps its libraries in the profile unless EMERICK_LIBRARY_DIR names a folder") {
    const QByteArray saved = qgetenv("EMERICK_LIBRARY_DIR");
    qunsetenv("EMERICK_LIBRARY_DIR");
    CHECK(libraryFolder("/profile") == std::filesystem::path{"/profile/libraries"});
    CHECK(libraryFolder({}).filename() == "libraries");
    qputenv("EMERICK_LIBRARY_DIR", "/data/libraries");
    CHECK(libraryFolder("/profile") == std::filesystem::path{"/data/libraries"});
    if (saved.isEmpty()) {
        qunsetenv("EMERICK_LIBRARY_DIR");
    } else {
        qputenv("EMERICK_LIBRARY_DIR", saved);
    }
}

TEST_CASE("library.list names each program's General MIDI sounds and a drum kit's keys, controls and pieces") {
    ensureApplication();
    const QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    Nav nav;
    const auto libraries = registerLibraries(registry, nav, dir.filePath("libraries").toStdString());
    const Json listed = registry.run("library.list").at("libraries");
    const auto program = [&](const std::string& library, const std::string& sound) {
        const Json& found = *std::ranges::find(listed, Json(library), [](const Json& entry) { return entry.at("id"); });
        return *std::ranges::find_if(found.at("programs"), [&](const Json& entry) {
            return std::ranges::contains(entry.at("gmSounds"), Json(sound));
        });
    };
    CHECK(program("vsco2-ce", "Trumpet").at("file") == "TrumpetSusVib.sfz");
    CHECK(program("salamander-grand-piano", "Acoustic Grand Piano").at("file") == "");
    const Json bass = program("black-and-blue-basses", "Electric Bass (finger)");
    CHECK(bass.at("file") == "Programs/03-babyblue_all.sfz");
    CHECK(bass.at("transpose") == 12);
    CHECK(std::ranges::fold_left(listed, std::size_t{0}, [](std::size_t count, const Json& library) {
              return std::ranges::fold_left(library.at("programs"), count, [](std::size_t sum, const Json& entry) {
                  return sum + entry.at("gmSounds").size();
              });
          }) == 45);

    const Json kit = listed[2].at("programs").at(0);
    CHECK(kit.at("gmKits").size() == 5);
    CHECK(std::ranges::contains(kit.at("keys"), Json{{"gmNote", 50}, {"note", 47}}));
    CHECK(std::ranges::contains(kit.at("controls"), Json{{"cc", 17}, {"value", 84}}));
    CHECK(kit.at("kitControls").at(0) ==
          Json{{"kit", "Brush"},
               {"controls", {{{"cc", 25}, {"value", 80}}, {{"cc", 26}, {"value", 80}}, {{"cc", 27}, {"value", 80}}}}});
    CHECK(kit.at("pieces").at(0) == Json{{"name", "kick"}, {"file", "Programs/03-kick.sfz"}, {"gmNotes", {35, 36}}});
    CHECK(kit.at("pieces").size() == 5);
}

TEST_CASE("installing all libraries installs every library in the catalog") {
    const std::vector<Library> catalog = curatedLibraries();
    const auto ids = libraryIdsToInstall({"all"}, catalog);
    CHECK(ids == (catalog | std::views::transform(&Library::id) | std::ranges::to<std::vector>()));
    CHECK(ids.size() == 15);
    CHECK(libraryIdsToInstall({"meatbass"}, catalog) == std::vector<std::string>{"meatbass"});
}
