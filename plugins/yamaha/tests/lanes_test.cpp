#include <QQmlContext>
#include <QQmlProperty>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

#include "emerick/yamaha/plugin.hpp"
#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

using Json = nlohmann::json;
using Bytes = std::vector<std::uint8_t>;

constexpr core::Ticks kBar = 4 * core::kTicksPerQuarter;

MainWindow& yamahaWindow() {
    static std::unique_ptr<yamaha::Listeners> listeners;
    static auto* main = new MainWindow(
        [](ProjectRig& rig) { listeners = yamaha::registerWith(rig.registry, *rig.controller, rig.nav); });
    return *main;
}

QString qid(const std::string& id) { return QString::fromStdString(id); }

const Json& dataOf(const MainWindow& main, const std::string& id) {
    return main.rig.controller->project().track(model::TrackId{id})->data;
}

std::string addTrack(MainWindow& main, const char* kind, const Json& events) {
    model::Track track;
    track.kind = kind;
    track.name = kind;
    track.data = events;
    track.midiIn = {.portId = kMidiPort, .channel = 0, .thru = false, .sysex = false};
    const std::string id = track.id.value;
    main.rig.controller->project().addTrack(std::move(track));
    MainWindow::settle();
    return id;
}

void removeTrack(MainWindow& main, const std::string& id) {
    main.rig.run("track.remove", {{"track", id}});
    MainWindow::settle();
}

QQuickItem* block(const MainWindow& main, const std::string& prefix, const std::string& id, core::Ticks at) {
    return main.item(QString::fromStdString(prefix + ".move:" + id + ":" + std::to_string(at)));
}

QObject* popupOf(QQuickItem* picker) { return picker->property("popup").value<QObject*>(); }

bool opened(QObject* popup) {
    return processUntil([&] { return popup->property("opened").toBool(); });
}

void type(const MainWindow& main, const QString& text) {
    for (const QChar letter : text) {
        QTest::keyClick(main.window, letter.toLatin1());
    }
}

QVariant attached(QQuickItem* item, const char* name) { return QQmlProperty(item, name, qmlContext(item)).read(); }

}  // namespace

TEST_CASE("a click on a chord block opens a picker that sets the root, the type and the bass from the keyboard") {
    MainWindow& main = yamahaWindow();
    const std::string id = addTrack(main, "yamaha.chord", Json::parse(R"([{"at":0,"chord":"C"}])"));
    main.click(block(main, "yamaha.chord", id, 0));
    QQuickItem* root = main.item("yamaha.chord.set:" + qid(id) + ":root");
    REQUIRE(opened(popupOf(root)));

    type(main, "bb");
    QTest::keyClick(main.window, Qt::Key_Return);
    REQUIRE(opened(popupOf(main.item("yamaha.chord.set:" + qid(id) + ":quality"))));
    type(main, "m7");
    QTest::keyClick(main.window, Qt::Key_Return);
    REQUIRE(opened(popupOf(main.item("yamaha.chord.set:" + qid(id) + ":bass"))));
    type(main, "d");
    QTest::keyClick(main.window, Qt::Key_Return);
    CHECK(processUntil([&] { return dataOf(main, id) == Json::parse(R"([{"at":0,"chord":"Bbm7/D"}])"); }));
    CHECK(processUntil([&] { return main.rig.nav.editingBlock().isEmpty(); }));

    QQuickItem* chord = block(main, "yamaha.chord", id, 0);
    main.window->grabWindow();
    const QPointF pastChord =
        chord->mapToScene({chord->width() + main.rig.nav.pixelsPerTick() * kBar, chord->height() / 2});
    QTest::mouseClick(main.window, Qt::LeftButton, Qt::NoModifier, pastChord.toPoint());
    REQUIRE(opened(popupOf(main.item("yamaha.chord.set:" + qid(id) + ":root"))));
    type(main, "F#m7/C#");
    QTest::keyClick(main.window, Qt::Key_Return);
    CHECK(
        processUntil([&] { return dataOf(main, id).size() == 2 && dataOf(main, id).at(1).at("chord") == "F#m7/C#"; }));
    removeTrack(main, id);
}

TEST_CASE("a chord played on the keyboard sets the block whose picker is open and closes the picker") {
    MainWindow& main = yamahaWindow();
    const std::string id = addTrack(main, "yamaha.chord", Json::parse(R"([{"at":0,"chord":"C"}])"));
    main.click(block(main, "yamaha.chord", id, 0));
    REQUIRE(opened(popupOf(main.item("yamaha.chord.set:" + qid(id) + ":root"))));
    for (const Bytes& bytes : std::vector<Bytes>{{0x93, 55, 90},
                                                 {0x93, 59, 90},
                                                 {0x93, 62, 90},
                                                 {0x93, 65, 90},
                                                 {0x83, 55, 0},
                                                 {0x83, 59, 0},
                                                 {0x83, 62, 0},
                                                 {0x83, 65, 0}}) {
        main.rig.midi->inject(kMidiPort, bytes, 0);
    }
    CHECK(processUntil([&] { return dataOf(main, id) == Json::parse(R"([{"at":0,"chord":"G7"}])"); }));
    CHECK(processUntil([&] { return !main.rig.nav.editingBlock().contains("track"); }));
    removeTrack(main, id);
}

TEST_CASE("the section picker lists only the sections a style plays") {
    MainWindow& main = yamahaWindow();
    const std::string id = addTrack(main, "yamaha.style", Json::parse(R"([{"at":0,"kind":"main","variation":0}])"));
    main.click(block(main, "yamaha.section", id, 0));
    QQuickItem* picker = main.item("yamaha.section.set:" + qid(id));
    REQUIRE(opened(popupOf(picker)));
    CHECK(picker->property("matches").toList().size() == 16);
    type(main, "ending 2");
    QTest::keyClick(main.window, Qt::Key_Return);
    CHECK(processUntil([&] { return dataOf(main, id) == Json::parse(R"([{"at":0,"kind":"ending","variation":1}])"); }));
    removeTrack(main, id);
}

TEST_CASE("a right click selects a block and opens its menu, and Backspace or Delete deletes it") {
    MainWindow& main = yamahaWindow();
    const std::string id = addTrack(
        main, "yamaha.chord", Json::parse(R"([{"at":0,"chord":"C"},{"at":3840,"chord":"F"},{"at":7680,"chord":"G"}])"));
    QQuickItem* first = block(main, "yamaha.chord", id, 0);
    main.click(first, Qt::RightButton);
    QObject* menu = main.item("yamaha.chord.edit:" + qid(id))->findChild<QObject*>("menu.yamaha.chord.context");
    REQUIRE(menu != nullptr);
    CHECK(opened(menu));
    CHECK(itemTitles(menu) == std::vector<QString>{"Change chord", "Split chord", "Delete chord"});
    CHECK_FALSE(main.menu("menu.laneContext")->property("visible").toBool());
    QMetaObject::invokeMethod(menu, "close");
    REQUIRE(processUntil([&] { return !menu->property("visible").toBool(); }));
    CHECK(processUntil([&] { return first->hasActiveFocus(); }));

    QTest::keyClick(main.window, Qt::Key_Backspace);
    CHECK(processUntil([&] { return dataOf(main, id).size() == 2; }));
    CHECK(main.rig.controller->project().track(model::TrackId{id}) != nullptr);
    main.click(block(main, "yamaha.chord", id, kBar), Qt::RightButton);
    QMetaObject::invokeMethod(menu, "close");
    REQUIRE(processUntil([&] { return !menu->property("visible").toBool(); }));
    QTest::keyClick(main.window, Qt::Key_Delete);
    CHECK(processUntil([&] { return dataOf(main, id) == Json::parse(R"([{"at":7680,"chord":"G"}])"); }));
    removeTrack(main, id);
}

TEST_CASE("a block's context menu splits at the playhead inside the block and at the click outside it") {
    MainWindow& main = yamahaWindow();
    const std::string id =
        addTrack(main, "yamaha.chord", Json::parse(R"([{"at":0,"chord":"C"},{"at":7680,"chord":"F"}])"));
    QObject* lane = main.item("yamaha.chord.edit:" + qid(id));
    QObject* menu = lane->findChild<QObject*>("menu.yamaha.chord.context");
    REQUIRE(menu != nullptr);
    const auto menuToAfterRightClick = [&] {
        main.click(block(main, "yamaha.chord", id, 0), Qt::RightButton);
        REQUIRE(opened(menu));
        QMetaObject::invokeMethod(menu, "close");
        REQUIRE(processUntil([&] { return !menu->property("visible").toBool(); }));
        return lane->property("menuTo").toLongLong();
    };
    main.rig.run("transport.locate", {{"position", 1000}});
    MainWindow::settle();
    CHECK(menuToAfterRightClick() == 1000);
    main.rig.run("transport.locate", {{"position", 9000}});
    MainWindow::settle();
    CHECK(menuToAfterRightClick() != 9000);
    removeTrack(main, id);
}

TEST_CASE("Delete in the picker of a clicked block deletes it until something is typed") {
    MainWindow& main = yamahaWindow();
    const std::string id =
        addTrack(main, "yamaha.chord", Json::parse(R"([{"at":0,"chord":"C"},{"at":3840,"chord":"F"}])"));
    main.click(block(main, "yamaha.chord", id, 0));
    REQUIRE(opened(popupOf(main.item("yamaha.chord.set:" + qid(id) + ":root"))));
    QTest::keyClick(main.window, Qt::Key_Delete);
    CHECK(processUntil([&] { return dataOf(main, id) == Json::parse(R"([{"at":3840,"chord":"F"}])"); }));
    CHECK(processUntil([&] { return main.rig.nav.editingBlock().isEmpty(); }));

    main.click(block(main, "yamaha.chord", id, kBar));
    REQUIRE(opened(popupOf(main.item("yamaha.chord.set:" + qid(id) + ":root"))));
    type(main, "b");
    QTest::keyClick(main.window, Qt::Key_Backspace);
    QTest::keyClick(main.window, Qt::Key_Backspace);
    CHECK(dataOf(main, id).size() == 1);
    removeTrack(main, id);
}

TEST_CASE(
    "a block's tooltip names the whole chord, waits for a hover and hides from a press until the pointer leaves") {
    MainWindow& main = yamahaWindow();
    const std::string id =
        addTrack(main, "yamaha.chord", Json::parse(R"([{"at":0,"chord":"Bbm7b5"},{"at":240,"chord":"C"}])"));
    QQuickItem* tiny = block(main, "yamaha.chord", id, 0);
    CHECK(attached(tiny, "ToolTip.text").toString().startsWith("Bbm7b5"));
    CHECK(attached(tiny, "ToolTip.delay").toInt() > 0);
    const auto shownLabel = [&](core::Ticks at) {
        QString shown;
        REQUIRE(QMetaObject::invokeMethod(main.item(QString::fromStdString("yamaha.chord.blocks:" + id)), "shownLabel",
                                          Q_RETURN_ARG(QString, shown), Q_ARG(qint64, at)));
        return shown;
    };
    CHECK(shownLabel(0) != "Bbm7b5");
    CHECK(shownLabel(240) == "C");

    QQuickItem* wide = block(main, "yamaha.chord", id, 240);
    main.hover(wide);
    CHECK(processUntil([&] { return attached(wide, "ToolTip.visible").toBool(); }));
    main.drag(wide, QPointF{main.rig.nav.pixelsPerTick() * kBar, 0});
    CHECK_FALSE(attached(block(main, "yamaha.chord", id, 0), "ToolTip.visible").toBool());
    CHECK(processUntil([&] { return dataOf(main, id).size() == 2 && dataOf(main, id).at(1).at("at") > 240; }));
    CHECK(main.rig.controller->project().undoName() == "move chord");
    main.rig.run("project.undo");
    CHECK(dataOf(main, id).at(1).at("at") == 240);
    removeTrack(main, id);
}
