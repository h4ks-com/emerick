#include "emerick/app/introspector.hpp"

#include <QQmlApplicationEngine>
#include <QTemporaryDir>
#include <QTimer>
#include <catch2/catch_test_macros.hpp>

#include "emerick/app/ui_api.hpp"
#include "qt_app.hpp"

using namespace emerick::app;

namespace {

constexpr auto kWindow = R"(
import QtQuick
import QtQuick.Controls

Window {
    objectName: "window.test"
    width: 320
    height: 200
    visible: true
    Column {
        Button {
            objectName: "test.button"
            property int clicks: 0
            text: "Press me"
            onClicked: clicks++
        }
        TextField { objectName: "test.field"; text: "before" }
        Rectangle { objectName: "test.hidden"; width: 10; height: 10; visible: false }
        Item { Label { objectName: "test.label"; text: "Inner" } }
        ComboBox { objectName: "test.picker"; model: ["One", "Two"] }
    }
}
)";

struct Fixture {
    bool application = (ensureApplication(), true);
    QTemporaryDir dir;
    Registry registry{dir.filePath("settings.ini")};
    QQmlApplicationEngine engine;

    Fixture() {
        engine.loadData(kWindow);
        REQUIRE(engine.rootObjects().size() == 1);
        registry.addSetting({.id = "audio.bufferSize",
                             .title = "Audio buffer size",
                             .type = SettingType::Integer,
                             .location = "settings.audio"});
    }
};

bool containsName(const Json& nodes, const std::string& name) {
    for (const Json& node : nodes) {
        if (node.value("objectName", "") == name ||
            (node.contains("children") && containsName(node["children"], name))) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("the tree lists named items and unrevealed registry entries") {
    Fixture fixture;
    const Json tree = uiTree(fixture.registry, true);
    const Json& window = tree.at("windows").at(0);
    CHECK(window.at("objectName") == "window.test");
    const Json& children = window.at("children");
    CHECK(children.at(0).at("objectName") == "test.button");
    CHECK(children.at(0).at("type") == "Button");
    CHECK(children.at(0).at("text") == "Press me");
    CHECK(children.at(0).at("rect").at("width").get<double>() > 0);
    CHECK(children.at(3).at("objectName") == "test.label");
    CHECK(tree.at("unrevealed").at(0).at("id") == "audio.bufferSize");
    CHECK(tree.at("unrevealed").at(0).at("location") == "settings.audio");

    CHECK(containsName(uiTree(fixture.registry, false).at("windows"), "test.field"));
    CHECK(uiTree(fixture.registry, false, "test.button").at("root").at("objectName") == "test.button");
}

TEST_CASE("find matches objectName or text") {
    Fixture fixture;
    CHECK(findItems("press").at(0).at("objectName") == "test.button");
    CHECK(findItems("test.f").at(0).at("objectName") == "test.field");
    CHECK(findItems("nothing here").empty());
}

TEST_CASE("properties read and write through QMetaObject") {
    Fixture fixture;
    CHECK(itemProperty("test.field", "text") == "before");
    setItemProperty("test.field", "text", "after");
    CHECK(itemProperty("test.field", "text") == "after");
    CHECK_THROWS_AS(setItemProperty("test.field", "missing", 1), AppError);
    CHECK_THROWS_AS(itemProperty("test.none", "text"), AppError);
}

TEST_CASE("invoke clicks a button through its own method") {
    Fixture fixture;
    invokeItem("test.button");
    CHECK(itemProperty("test.button", "clicks") == 1);
    CHECK_THROWS_AS(invokeItem("test.hidden"), AppError);
}

TEST_CASE("invoke opens a picker's list the way a click does") {
    Fixture fixture;
    invokeItem("test.picker");
    const QObject* picker = fixture.engine.rootObjects().front()->findChild<QObject*>("test.picker");
    CHECK(picker->property("popup").value<QObject*>()->property("visible").toBool());
}

TEST_CASE("waiting for an item returns once it shows with its geometry") {
    Fixture fixture;
    QTimer::singleShot(20, [] { setItemProperty("test.hidden", "visible", true); });
    const auto seen = waitForVisibleItem("test.hidden", std::chrono::milliseconds{2000});
    REQUIRE(seen);
    CHECK(seen->at("visible") == true);
    CHECK(seen->at("rect").at("width") == 10);
    CHECK_FALSE(waitForVisibleItem("test.none", std::chrono::milliseconds{50}));
}

TEST_CASE("UiApi reveals a registry entry and reports a missing item as an error") {
    Fixture fixture;
    bool revealed = false;
    fixture.registry.addLocation({.id = "test.area", .reveal = [&revealed] { revealed = true; }});
    fixture.registry.addAction({.id = "test.button",
                                .title = "Press",
                                .run = [](const Json&) { return Json::object(); },
                                .location = "test.area"});
    const UiApi ui{fixture.registry};
    const Json shown = ui.reveal("test.button");
    CHECK(revealed);
    CHECK(shown.at("chain") == Json::array({"test.area"}));
    CHECK(shown.at("item").at("objectName") == "test.button");
    fixture.registry.addAction({.id = "test.gone",
                                .title = "Gone",
                                .run = [](const Json&) { return Json::object(); },
                                .location = "test.area"});
    CHECK(ui.reveal("test.gone").contains("error"));
    CHECK(ui.find("press").at("entries").at(0).at("kind") == "action");
}
