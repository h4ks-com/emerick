#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

#include "emerick/app/core_actions.hpp"
#include "emerick/app/qml_exposure.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

// QML singletons are registered once per process, so every test shares one window over one project.
struct MainWindow {
    ProjectRig rig;
    TrackListModel tracks{*rig.controller, rig.registry};
    PeakCache peaks;
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;

    MainWindow() {
        registerCoreActions(rig.registry, rig.nav, {});
        exposeToQml(engine, rig.registry, rig.nav, *rig.controller, tracks, peaks);
        engine.loadFromModule("Emerick.Ui", "Main");
        REQUIRE(engine.rootObjects().size() == 1);
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        REQUIRE(window != nullptr);
        REQUIRE(QTest::qWaitForWindowExposed(window));
    }

    static MainWindow& instance() {
        // We never delete the window, because destroying Qt Quick during static teardown crashes.
        static auto* main = new MainWindow;
        return *main;
    }

    [[nodiscard]] QQuickItem* item(const QString& objectName) const {
        auto* found = window->findChild<QQuickItem*>(objectName);
        REQUIRE(found != nullptr);
        return found;
    }

    [[nodiscard]] QPoint center(const QString& objectName) const {
        QQuickItem* target = item(objectName);
        return target->mapToScene(QPointF{target->width() / 2, target->height() / 2}).toPoint();
    }

    void click(const QString& objectName) const {
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center(objectName));
    }

    [[nodiscard]] QString shown(const QString& objectName) const {
        return item(objectName)->property("value").toString();
    }
};

}  // namespace

TEST_CASE("the play button toggles play and pause, and the display shows the located position") {
    MainWindow& main = MainWindow::instance();

    main.click("transport.togglePlay");
    CHECK(processUntil([&] { return main.rig.controller->playing(); }));
    main.click("transport.togglePlay");
    CHECK(processUntil([&] { return !main.rig.controller->playing(); }));

    main.rig.run("transport.locate", {{"position", "5.2.250"}});
    CHECK(processUntil([&] { return main.shown("transport.position.bar") == "5"; }));
    CHECK(main.shown("transport.position.beat") == "2");
    CHECK(main.shown("transport.position.division") == "2");
    CHECK(main.shown("transport.position.tick") == "11");
}

TEST_CASE("clicking the tempo edits it in place, and Return there keeps the playhead") {
    MainWindow& main = MainWindow::instance();
    main.rig.run("transport.locate", {{"position", "3.1.0"}});

    QTest::mouseDClick(main.window, Qt::LeftButton, Qt::NoModifier, main.center("project.setTempo"));
    REQUIRE(processUntil([&] { return main.item("project.setTempo.bpm:text")->hasActiveFocus(); }));
    QTest::keyClick(main.window, '9');
    QTest::keyClick(main.window, '6');
    QTest::keyClick(main.window, Qt::Key_Return);

    CHECK(processUntil([&] { return main.rig.controller->project().document().tempo.bpm == 96.0; }));
    CHECK(main.item("project.setTempo.bpm")->property("displayText").toString() == "96");
    CHECK(main.rig.controller->positionTicks() > 0);
}
