#include "emerick/model/automation.hpp"

#include <QTest>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;
using Catch::Approx;

namespace {

QString qid(const std::string& id) { return QString::fromStdString(id); }

// Tests share one window, so each starts from a project with only its own track, which stays in view.
std::string onlyTrack(MainWindow& main) {
    while (!main.rig.controller->project().document().tracks.empty()) {
        main.rig.run("track.remove", {{"track", main.rig.controller->project().document().tracks.front().id.value}});
    }
    return main.rig.run("track.addAudio").at("id");
}

struct Lanes {
    MainWindow& main = MainWindow::instance();
    std::string track = onlyTrack(main);

    Lanes() {
        main.rig.nav.setScrollTicks(0);
        main.rig.nav.setTrackHeight(kMaxTrackHeight);
        main.rig.nav.setAutomationOpen(true);
        MainWindow::settle();
    }

    [[nodiscard]] QQuickItem* lane() const { return main.item("automation.lane:" + qid(track)); }

    [[nodiscard]] const model::AutomationLane* gain() const {
        return model::findLane(*main.rig.controller->project().track(model::TrackId{track}), "gain");
    }

    [[nodiscard]] double call(const char* function, double value) const {
        QVariant result;
        QMetaObject::invokeMethod(lane(), function, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, value));
        return result.toDouble();
    }

    // The scene point in the lane at ticks and value.
    [[nodiscard]] QPointF at(core::Ticks ticks, double value) const {
        return lane()->mapToScene({call("xOf", static_cast<double>(ticks)), call("yOf", value)});
    }

    void click(QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const {
        main.send(QEvent::MouseButtonPress, point, Qt::LeftButton, modifiers);
        main.send(QEvent::MouseButtonRelease, point, Qt::NoButton, modifiers);
        MainWindow::settle();
    }
};

}  // namespace

TEST_CASE("A shows the automation lanes, and the header picks the mode and the lane") {
    MainWindow& main = MainWindow::instance();
    main.rig.nav.setAutomationOpen(false);
    const std::string track = onlyTrack(main);
    main.rig.nav.setTrackHeight(kMaxTrackHeight);
    QTest::keyClick(main.window, Qt::Key_A);
    CHECK(main.rig.nav.automationOpen());
    CHECK(main.item("automation.lane:" + qid(track))->isVisible());
    CHECK(main.item("automation.setMode:" + qid(track))->property("text") == "Read");
    CHECK(main.item("automation.showLane:" + qid(track))->property("text") == "Volume");
    main.rig.run("automation.showLane", {{"track", track}, {"param", "pan"}});
    CHECK(processUntil([&] { return main.item("automation.showLane:" + qid(track))->property("text") == "Pan"; }));
    main.rig.run("automation.setMode", {{"track", track}, {"mode", "latch"}});
    CHECK(processUntil([&] { return main.item("automation.setMode:" + qid(track))->property("text") == "Latch"; }));
    MainWindow::settle();
}

TEST_CASE("a click on the lane adds a point on the beat where it was clicked") {
    Lanes lanes;
    lanes.click(lanes.at(1000, -12.0));
    REQUIRE(lanes.gain() != nullptr);
    REQUIRE(lanes.gain()->points.size() == 1);
    CHECK(lanes.gain()->points[0].tick == 960);
    CHECK(lanes.gain()->points[0].value == Approx(-12.0).margin(0.5));
    CHECK(lanes.main.rig.controller->project().undoName() == "add automation point");
}

TEST_CASE("dragging a point moves it live in time and value as one undo step") {
    Lanes lanes;
    const std::string point =
        lanes.main.rig.run("automation.addPoint", {{"track", lanes.track}, {"tick", 960}, {"value", -12.0}})
            .at("point");
    MainWindow::settle();
    const QPointF from = lanes.at(960, -12.0);
    const QPointF to = lanes.at(1920, -6.0);
    lanes.main.send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::NoModifier);
    for (int step = 1; step <= 5; ++step) {
        lanes.main.send(QEvent::MouseMove, from + (to - from) * step / 5, Qt::LeftButton, Qt::NoModifier);
    }
    CHECK(processUntil([&] { return lanes.gain()->points[0].tick == 1920; }));
    CHECK(lanes.gain()->points[0].value == Approx(-6.0).margin(0.5));
    lanes.main.send(QEvent::MouseButtonRelease, to, Qt::NoButton, Qt::NoModifier);
    MainWindow::settle();

    CHECK(lanes.main.rig.controller->project().undoName() == "move automation points");
    lanes.main.rig.run("project.undo");
    CHECK(lanes.gain()->points[0].tick == 960);
    CHECK(lanes.gain()->points[0].id.value == point);
    CHECK(lanes.main.rig.controller->project().undoName() == "add automation point");
}

TEST_CASE("Alt-click deletes a point and Delete removes the selected points") {
    Lanes lanes;
    lanes.main.rig.run("automation.addPoint", {{"track", lanes.track}, {"tick", 0}, {"value", 0.0}});
    lanes.main.rig.run("automation.addPoint", {{"track", lanes.track}, {"tick", 1920}, {"value", -6.0}});
    lanes.main.rig.run("automation.addPoint", {{"track", lanes.track}, {"tick", 3840}, {"value", -24.0}});
    MainWindow::settle();
    lanes.click(lanes.at(1920, -6.0), Qt::AltModifier);
    REQUIRE(lanes.gain()->points.size() == 2);

    lanes.click(lanes.at(3840, -24.0));
    CHECK(lanes.main.rig.nav.selectedPoints() == QStringList{qid(lanes.gain()->points[1].id.value)});
    QTest::keyClick(lanes.main.window, Qt::Key_Delete);
    REQUIRE(lanes.gain()->points.size() == 1);
    CHECK(lanes.gain()->points[0].tick == 0);
    CHECK(lanes.main.rig.controller->project().track(model::TrackId{lanes.track}) != nullptr);
    MainWindow::settle();
}
