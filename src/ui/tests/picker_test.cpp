#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "emerick/app/core_actions.hpp"
#include "emerick/app/qml_exposure.hpp"
#include "project_rig.hpp"

using namespace emerick::app;
using namespace emerick::app::test;

namespace {

std::atomic<int> polishLoops{0};

void countPolishLoops(QtMsgType /*type*/, const QMessageLogContext& /*context*/, const QString& message) {
    if (message.contains(QStringLiteral("polish() loop"))) {
        ++polishLoops;
    }
}

}  // namespace

TEST_CASE("a picker with more choices than fit below it opens without a polish loop") {
    ProjectRig rig;
    TrackListModel tracks{*rig.controller, rig.registry};
    PeakCache peaks;
    QQmlApplicationEngine engine;
    registerCoreActions(rig.registry, rig.nav, {});
    exposeToQml(engine, rig.registry, rig.nav, *rig.controller, tracks, peaks);
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import Emerick.Ui
        Window {
            width: 1280
            height: 800
            visible: true
            SearchCombo {
                objectName: "picker"
                y: 155
                width: 236
                model: Array.from({length: 60}, (_, index) => ({id: String(index), name: "Device " + index}))
            }
        })",
                      QUrl{});
    std::unique_ptr<QObject> root{component.create()};
    REQUIRE(root != nullptr);
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    REQUIRE(QTest::qWaitForWindowExposed(window));
    auto* picker = window->findChild<QQuickItem*>(QStringLiteral("picker"));
    REQUIRE(picker != nullptr);

    const QtMessageHandler previous = qInstallMessageHandler(countPolishLoops);
    QMetaObject::invokeMethod(picker->property("popup").value<QObject*>(), "open");
    QTest::qWait(500);
    qInstallMessageHandler(previous);

    CHECK(picker->property("popup").value<QObject*>()->property("opened").toBool());
    CHECK(polishLoops == 0);
}
