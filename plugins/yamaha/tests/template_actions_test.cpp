#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>

#include "emerick/yamaha/keyboard_template.hpp"
#include "yamaha_rig.hpp"

using namespace emerick;
using namespace emerick::yamaha;
using namespace emerick::yamaha::test;

TEST_CASE("installing the keyboard template writes it into the MIDI folder of the User drive") {
    YamahaRig rig;
    const std::filesystem::path drive = rig.path("NO NAME");
    for (const char* folder : {"MIDI", "STYLE", "REGIST"}) {
        std::filesystem::create_directories(drive / folder);
    }
    const Json result = rig.run("yamaha.template.install", {{"drive", drive.string()}});
    CHECK(result.at("path") == (drive / "MIDI" / "Emerick.mis").string());
    CHECK(fileBytes((drive / "MIDI" / "Emerick.mis").string()) == encodeKeyboardTemplate({}));
    CHECK(result.at("steps").get<std::string>().contains("Menu > MIDI"));
}

TEST_CASE("installing the keyboard template refuses a folder that is no User drive") {
    YamahaRig rig;
    std::filesystem::create_directories(rig.path("stick") / "MIDI");
    CHECK_THROWS_AS(rig.run("yamaha.template.install", {{"drive", rig.path("stick").string()}}), app::AppError);
}

TEST_CASE("saving the keyboard template writes it to the chosen file") {
    YamahaRig rig;
    const std::filesystem::path path = rig.path("Emerick.mis");
    rig.run("yamaha.template.save", {{"path", path.string()}});
    CHECK(fileBytes(path.string()) == encodeKeyboardTemplate({}));

    rig.registry.setSetting("yamaha.controllerChannel", 3);
    rig.run("yamaha.template.save", {{"path", path.string()}});
    CHECK(fileBytes(path.string()) == encodeKeyboardTemplate({.controllerChannel = 3}));
}
