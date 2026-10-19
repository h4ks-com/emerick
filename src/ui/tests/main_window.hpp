#pragma once

#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "emerick/app/console.hpp"
#include "emerick/app/core_actions.hpp"
#include "emerick/app/qml_exposure.hpp"
#include "project_rig.hpp"

namespace emerick::app::test {

// QML singletons are registered once per process, so every test shares one window over one project.
struct MainWindow {
    ProjectRig rig;
    TrackListModel tracks{*rig.controller, rig.registry};
    PeakCache peaks;
    UiApi ui{rig.registry};
    LuaHost lua{rig.registry, ui};
    Console console{rig.registry, lua};
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;

    MainWindow() {
        registerCoreActions(rig.registry, rig.nav, {});
        registerConsoleActions(rig.registry, rig.nav, console);
        exposeToQml(engine, rig.registry, rig.nav, *rig.controller, tracks, peaks, &console);
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

    void send(QEvent::Type type, QPointF at, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) const {
        const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event{type, at, window->mapToGlobal(at), button, buttons, modifiers};
        QCoreApplication::sendEvent(window, &event);
    }

    // Presses at the center of target, moves by offset in steps and releases, holding modifiers throughout.
    void drag(QQuickItem* target, QPointF offset, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const {
        const QPointF from = target->mapToScene({target->width() / 2, target->height() / 2});
        constexpr int kSteps = 5;
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, modifiers);
        for (int step = 1; step <= kSteps; ++step) {
            send(QEvent::MouseMove, from + offset * step / kSteps, Qt::LeftButton, modifiers);
        }
        send(QEvent::MouseButtonRelease, from + offset, Qt::NoButton, modifiers);
        settle();
    }

    void click(QQuickItem* target, Qt::MouseButton button = Qt::LeftButton) const {
        const QPointF at = target->mapToScene({target->width() / 2, target->height() / 2});
        for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
            QMouseEvent event{type,
                              at,
                              window->mapToGlobal(at),
                              button,
                              type == QEvent::MouseButtonPress ? Qt::MouseButtons{button} : Qt::NoButton,
                              Qt::NoModifier};
            QCoreApplication::sendEvent(window, &event);
        }
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
