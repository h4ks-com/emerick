#pragma once

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "emerick/app/core_actions.hpp"
#include "emerick/app/qml_exposure.hpp"
#include "project_rig.hpp"

namespace emerick::app::test {

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

    // The visible item with objectName, once the views have caught up.
    [[nodiscard]] QQuickItem* item(const QString& objectName) const {
        QQuickItem* found = nullptr;
        processUntil([&] {
            found = find(window->contentItem(), objectName);
            return found != nullptr;
        });
        REQUIRE(found != nullptr);
        return found;
    }

    // Views put delegates in the item tree without a QObject parent, so we walk the visible child items.
    static QQuickItem* find(QQuickItem* root, const QString& objectName) {
        if (!root->isVisible()) {
            return nullptr;
        }
        if (root->objectName() == objectName) {
            return root;
        }
        for (QQuickItem* child : root->childItems()) {
            if (QQuickItem* found = find(child, objectName)) {
                return found;
            }
        }
        return nullptr;
    }

    // Deletes the delegates an edit dropped, which Qt cannot do safely once the process exits.
    static void settle() {
        constexpr int kCloseMs = 200;
        processUntil([] { return false; }, kCloseMs);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }

    void click(QQuickItem* target, Qt::MouseButton button = Qt::LeftButton) const {
        QTest::mouseClick(window, button, Qt::NoModifier,
                          target->mapToScene({target->width() / 2, target->height() / 2}).toPoint());
    }
};

inline std::vector<QString> itemTitles(QObject* menu) {
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

inline std::vector<QString> registryTitles(const Registry& registry, const QString& menu) {
    std::vector<QString> titles;
    for (const QVariant& entry : registry.menus().value(menu).toList()) {
        titles.push_back(entry.toMap().value("title").toString());
    }
    return titles;
}

}  // namespace emerick::app::test
