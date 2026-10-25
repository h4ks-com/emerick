#include <QObject>
#include <QString>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

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

TEST_CASE("installing or saving the keyboard template tells the user where it went or why it failed") {
    YamahaRig rig;
    std::vector<std::pair<QString, QString>> notices;
    QObject::connect(&rig.registry, &app::Registry::noticeRaised,
                     [&](const QString& title, const QString& text) { notices.emplace_back(title, text); });
    const std::filesystem::path drive = rig.path("NO NAME");
    for (const char* folder : {"MIDI", "STYLE", "REGIST"}) {
        std::filesystem::create_directories(drive / folder);
    }
    const auto text = [&](std::size_t index) { return notices.at(index).second; };
    const auto asQString = [](const std::filesystem::path& path) { return QString::fromStdString(path.string()); };
    rig.registry.runAction("yamaha.template.install", {{"drive", asQString(drive)}});
    REQUIRE(notices.size() == 1);
    CHECK(notices.at(0).first == "Install keyboard template");
    CHECK(text(0).contains(asQString(drive / "MIDI" / "Emerick.mis")));
    CHECK(text(0).contains("Menu > MIDI"));

    const std::filesystem::path saved = rig.path("Emerick.mis");
    rig.registry.runAction("yamaha.template.save", {{"path", asQString(saved)}});
    REQUIRE(notices.size() == 2);
    CHECK(text(1).contains(asQString(saved)));

    rig.registry.runAction("yamaha.template.install", {{"drive", asQString(rig.path("stick"))}});
    REQUIRE(notices.size() == 3);
    CHECK(text(2).contains("no MIDI, STYLE and REGIST folders"));
}
