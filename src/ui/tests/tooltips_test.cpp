#include <QQmlContext>
#include <QQmlProperty>
#include <QStringList>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "emerick/model/json.hpp"
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

// We also read a ToolTip child, because a tooltip that wraps cannot use the attached one.
QString toolTip(QQuickItem& item) {
    for (const QObject* child : item.children()) {
        if (child->inherits("QQuickToolTip")) {
            return child->property("text").toString();
        }
    }
    return QQmlProperty(&item, "ToolTip.text", qmlContext(&item)).read().toString();
}

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

TEST_CASE("a track's route error shows as a warning badge in its header and the inspector, with a wrapping tooltip") {
    MainWindow& main = MainWindow::instance();
    model::Document document;
    model::Track phones;
    phones.kind = "audio";
    phones.name = "Phones";
    phones.audioOut = {"keyboard", {1, 2}};
    document.tracks = {phones};
    const auto folder = main.rig.path("errors.erk");
    model::saveProject(document, folder);
    main.rig.run("project.open", {{"path", folder.string()}});
    main.rig.run("track.select", {{"track", phones.id.value}});
    MainWindow::settle();

    const QString id = QString::fromStdString(phones.id.value);
    QQuickItem* badge = main.item("track.errors:" + id);
    CHECK(badge->property("message").toString().startsWith("Digital Keyboard runs at 44100 Hz"));
    CHECK(main.item("inspector.trackErrors")->property("message") == badge->property("message"));
    main.hover(badge);
    auto* tip = badge->findChild<QObject*>("track.errors:" + id + ":tip");
    REQUIRE(tip != nullptr);
    REQUIRE(processUntil([&] { return tip->property("opened").toBool(); }));
    CHECK(tip->property("width").toReal() <= 320);
    CHECK(tip->property("contentItem").value<QObject*>()->property("lineCount").toInt() > 1);
    QQuickItem* icon = main.item("track.select:" + id);
    const QRectF badgeArea = badge->mapRectToItem(icon, QRectF(0, 0, badge->width(), badge->height()));
    CHECK(QRectF(0, 0, icon->width(), icon->height()).intersects(badgeArea));
    QQuickItem* name = main.item("track.name:" + id);
    const qreal nameWidth = name->width();

    main.rig.run("track.setAudioOut", {{"track", phones.id.value}, {"device", "interface"}});
    MainWindow::settle();
    CHECK_FALSE(badge->isVisible());
    CHECK(name->width() == nameWidth);
    CHECK(MainWindow::find(main.window->contentItem(), "inspector.trackErrors") == nullptr);
    main.rig.run("project.new");
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
