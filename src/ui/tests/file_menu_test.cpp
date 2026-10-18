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

    [[nodiscard]] QObject* menu(const QString& objectName) const {
        auto* found = window->findChild<QObject*>(objectName);
        REQUIRE(found != nullptr);
        return found;
    }
};

std::vector<QString> itemTitles(QObject* menu) {
    std::vector<QString> titles;
    const int count = menu->property("count").toInt();
    for (int index = 0; index < count; ++index) {
        QQuickItem* item = nullptr;
        QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, index));
        REQUIRE(item != nullptr);
        titles.push_back(item->property("text").toString());
    }
    return titles;
}

std::vector<QString> registryTitles(const Registry& registry, const QString& menu) {
    std::vector<QString> titles;
    for (const QVariant& entry : registry.menus().value(menu).toList()) {
        titles.push_back(entry.toMap().value("title").toString());
    }
    return titles;
}

}  // namespace

TEST_CASE("the File menu and the header's project menu list their registry actions in order") {
    MainWindow& main = MainWindow::instance();
    const auto file = registryTitles(main.rig.registry, "File");
    REQUIRE(file.size() > 6);
    CHECK(processUntil([&] { return itemTitles(main.menu("menu.file")) == file; }));
    CHECK(main.menu("project.saveAs") != nullptr);
    CHECK(main.menu("project.openRecent")->property("title").toString() == "Open recent project");

    const auto project = registryTitles(main.rig.registry, "Project");
    CHECK(project == std::vector<QString>{"New project", "Open project", "Open recent project", "Save project as",
                                          "Export for Logic",
                                          main.rig.registry.action("project.reveal")->title.c_str()});
    main.rig.run("project.showMenu");
    QObject* header = main.menu("menu.project");
    CHECK(processUntil([&] { return header->property("opened").toBool(); }));
    CHECK(itemTitles(header) == project);
    main.rig.run("project.showMenu");
    CHECK(processUntil([&] { return !header->property("visible").toBool(); }));
}

TEST_CASE("the File menu's shortcut saves the project and the title drops the edited dot") {
    MainWindow& main = MainWindow::instance();
    main.rig.run("track.addMidi");
    REQUIRE(processUntil([&] { return main.window->title().endsWith("•"); }));
    QTest::keyClick(main.window, Qt::Key_S, Qt::ControlModifier);
    CHECK(processUntil([&] { return !main.rig.controller->edited(); }, 1000));
    CHECK(main.window->title() == "Untitled 1");
}
