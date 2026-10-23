#include <QQmlContext>
#include <QQmlProperty>
#include <QStringList>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

// Menu titles and menu items name their action in their text, so they need no tooltip.
bool isControl(const QQuickItem& item) {
    if (item.inherits("QQuickMenuBarItem") || item.inherits("QQuickMenuItem")) {
        return false;
    }
    return item.inherits("QQuickAbstractButton") || item.inherits("QQuickSlider") || item.inherits("QQuickTextField") ||
           item.inherits("QQuickTextArea") || QString{item.metaObject()->className()}.startsWith("DragNumber");
}

QString toolTip(QQuickItem& item) { return QQmlProperty(&item, "ToolTip.text", qmlContext(&item)).read().toString(); }

// The objectName or class of every visible control under root that shows no tooltip.
void collectUntitled(QQuickItem& root, QStringList& untitled) {
    if (!root.isVisible()) {
        return;
    }
    if (isControl(root) && toolTip(root).isEmpty()) {
        untitled.append(root.objectName().isEmpty() ? root.metaObject()->className() : root.objectName());
    }
    for (QQuickItem* child : root.childItems()) {
        collectUntitled(*child, untitled);
    }
}

std::string untitledControls(const MainWindow& main) {
    MainWindow::settle();
    QStringList untitled;
    collectUntitled(*main.window->contentItem(), untitled);
    return untitled.join(", ").toStdString();
}

}  // namespace

TEST_CASE("every visible control in the main window and on every settings page shows a tooltip") {
    MainWindow& main = MainWindow::instance();
    main.rig.run("track.addAudio");
    main.rig.run("track.addMidi");
    main.rig.nav.setMixerOpen(true);
    main.rig.nav.setConsoleOpen(true);
    CHECK(untitledControls(main) == std::string{});

    main.rig.run("settings.open");
    for (const QVariant& section : main.rig.registry.sections("settings")) {
        main.rig.run("settings.show." + section.toMap().value("key").toString().toStdString());
        INFO(section.toMap().value("key").toString().toStdString());
        CHECK(untitledControls(main) == std::string{});
    }
    main.rig.run("settings.close");
    MainWindow::settle();
}

TEST_CASE("a button's tooltip hides once it is pressed, so it never covers the menu the button opens") {
    MainWindow& main = MainWindow::instance();
    QQuickItem* button = main.item("project.showMenu");
    main.click(button);
    CHECK(button->property("tipDismissed").toBool());
    CHECK_FALSE(QQmlProperty(button, "ToolTip.visible", qmlContext(button)).read().toBool());
    main.rig.run("project.showMenu");
    MainWindow::settle();
}
