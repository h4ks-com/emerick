#include "emerick/app/path_access.hpp"

#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>

#include "fuzz_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

namespace fs = std::filesystem;

struct Folders {
    QTemporaryDir dir;
    fs::path base = fs::canonical(dir.path().toStdString());
    fs::path root = base / "work";
    fs::path outside = base / "outside";

    Folders() {
        fs::create_directories(root / "takes");
        fs::create_directories(outside);
        fs::create_directories(base / "work2");
        std::ofstream(outside / "secret.wav") << "x";
        std::ofstream(root / "takes" / "a.wav") << "x";
    }
};

}  // namespace

TEST_CASE("a path inside the root comes back canonical") {
    const Folders folders;
    CHECK(pathInside(folders.root / "takes" / "a.wav", folders.root) == folders.root / "takes" / "a.wav");
    CHECK(pathInside(folders.root / "takes" / ".." / "takes" / "./a.wav", folders.root) ==
          folders.root / "takes" / "a.wav");
    CHECK(pathInside(folders.root / "new" / "song.erk", folders.root) == folders.root / "new" / "song.erk");
    CHECK(pathInside(folders.root, folders.root) == folders.root);
}

TEST_CASE("a path outside the root is refused however it is spelled") {
    const Folders folders;
    CHECK_FALSE(pathInside(folders.outside / "secret.wav", folders.root));
    CHECK_FALSE(pathInside(folders.base / "work2" / "a.wav", folders.root));
    CHECK_FALSE(pathInside(folders.root / ".." / "outside" / "secret.wav", folders.root));
    CHECK_FALSE(pathInside(folders.root / "missing" / ".." / ".." / "outside", folders.root));
    CHECK_FALSE(pathInside(folders.base, folders.root));
    CHECK_FALSE(pathInside("", folders.root));
}

TEST_CASE("a relative path resolves against the working folder") {
    const Folders folders;
    const fs::path previous = fs::current_path();
    fs::current_path(folders.root);
    CHECK(pathInside("takes/a.wav", folders.root) == folders.root / "takes" / "a.wav");
    CHECK_FALSE(pathInside("../outside/secret.wav", folders.root));
    fs::current_path(previous);
}

TEST_CASE("symlinks resolve before the check, and one that points nowhere is refused") {
    const Folders folders;
    fs::create_directory_symlink(folders.outside, folders.root / "escape");
    fs::create_symlink(folders.outside / "secret.wav", folders.root / "secret.wav");
    fs::create_symlink(folders.outside / "later.wav", folders.root / "dangling.wav");
    fs::create_directory_symlink(folders.root / "takes", folders.root / "inner");
    CHECK_FALSE(pathInside(folders.root / "escape" / "secret.wav", folders.root));
    CHECK_FALSE(pathInside(folders.root / "escape" / "new.wav", folders.root));
    CHECK_FALSE(pathInside(folders.root / "secret.wav", folders.root));
    CHECK_FALSE(pathInside(folders.root / "dangling.wav", folders.root));
    CHECK(pathInside(folders.root / "inner" / "a.wav", folders.root) == folders.root / "takes" / "a.wav");
}

TEST_CASE("agent requests name paths only inside the agent roots and the person names any path") {
    ProjectRig rig;
    const Folders folders;
    rig.registry.setAgentRoots({.read = folders.root, .write = folders.root});
    const std::string outside = (folders.outside / "Song").string();
    const auto asAgent = [&rig](ActionSource origin, const std::string& id, const Json& params) {
        return rig.registry.runFrom(origin, [&] { return rig.run(id, params); });
    };

    CHECK_THROWS_AS(asAgent(ActionSource::Mcp, "project.saveAs", {{"path", outside}}), AppError);
    CHECK_THROWS_AS(asAgent(ActionSource::Lua, "export.mix", {{"path", outside + ".wav"}}), AppError);
    CHECK_THROWS_AS(rig.registry.run("export.midi", {{"path", outside + ".mid"}}, ActionSource::Mcp), AppError);
    CHECK_FALSE(fs::exists(folders.outside / "Song.erk"));
    CHECK_FALSE(fs::exists(folders.outside / "Song.wav"));

    const std::string track = rig.run("track.addAudio").at("id");
    writeConstant(folders.outside / "take.wav", 480, 0.5F);
    CHECK_THROWS_AS(asAgent(ActionSource::Mcp, "clip.import",
                            {{"track", track}, {"path", (folders.outside / "take.wav").string()}}),
                    AppError);

    asAgent(ActionSource::Mcp, "project.saveAs", {{"path", (folders.root / "Song").string()}});
    CHECK(fs::exists(folders.root / "Song.erk" / "project.json"));
    asAgent(ActionSource::Ui, "project.saveAs", {{"path", outside}});
    CHECK(fs::exists(folders.outside / "Song.erk" / "project.json"));
}

TEST_CASE("agent settings, scripts and Lua name files only inside the read root") {
    FuzzRig rig;
    const Folders folders;
    rig.registry.setAgentRoots({.read = folders.root, .write = std::nullopt});
    const std::string secret = (folders.outside / "secret.wav").string();
    std::ofstream(folders.outside / "steal.lua") << "return 1";
    const auto asAgent = [&rig](const std::function<Json()>& call) {
        return rig.registry.runFrom(ActionSource::Mcp, call);
    };

    CHECK_THROWS_AS(asAgent([&] { return rig.registry.setSetting("metronome.beatSample", secret); }), AppError);
    CHECK(asAgent([&] { return rig.registry.setSetting("metronome.beatSample", ""); }) == "");
    CHECK(rig.registry.setSetting("metronome.beatSample", secret) == secret);
    CHECK_THROWS_AS(
        rig.registry.run("script.run", {{"path", (folders.outside / "steal.lua").string()}}, ActionSource::Mcp),
        AppError);
    CHECK(rig.lua.eval("return emerick.setting('metronome.accentSample', '" + secret + "')").at("ok") == false);
}

TEST_CASE("agent folder settings name folders only inside the read root") {
    FuzzRig rig;
    const Folders folders;
    rig.registry.setAgentRoots({.read = folders.root, .write = std::nullopt});
    rig.registry.addSetting({.id = "test.folders",
                             .title = "Folders",
                             .type = SettingType::List,
                             .defaultValue = Json::array(),
                             .merge = readableFolders(rig.registry)});
    const auto asAgent = [&rig](const Json& value) {
        return rig.registry.runFrom(ActionSource::Mcp, [&] { return rig.registry.setSetting("test.folders", value); });
    };

    CHECK_THROWS_AS(asAgent(Json::array({folders.root.string(), folders.outside.string()})), AppError);
    CHECK(asAgent(Json::array({(folders.root / "takes").string()})) ==
          Json::array({(folders.root / "takes").string()}));
    CHECK(rig.registry.setSetting("test.folders", Json::array({folders.outside.string()})) ==
          Json::array({folders.outside.string()}));
}
