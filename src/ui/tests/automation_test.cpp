#include "emerick/model/automation.hpp"

#include <QColor>
#include <QImage>
#include <QTest>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>
#include <vector>

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

TEST_CASE("a gain lane gives the dB near 0 more room than the floor, and a cutoff lane spreads octaves evenly") {
    Lanes lanes;
    const auto rise = [&](double low, double high) { return lanes.call("yOf", low) - lanes.call("yOf", high); };
    CHECK(rise(-6, 0) > 2 * rise(-60, -54));
    CHECK(lanes.call("yOf", lanes.call("valueAt", lanes.call("yOf", -3))) == Approx(lanes.call("yOf", -3)));
    lanes.click(lanes.at(1000, -3.0));
    REQUIRE(lanes.gain() != nullptr);
    CHECK(lanes.gain()->points.back().value == Approx(-3.0).margin(0.5));

    const std::string filter = lanes.main.rig.run("insert.add", {{"track", lanes.track}, {"type", "filter"}}).at("id");
    lanes.main.rig.nav.setEditingInserts({});
    lanes.main.rig.run("automation.showLane", {{"track", lanes.track}, {"param", filter + ".cutoff"}});
    REQUIRE(processUntil([&] {
        return lanes.main.item("automation.showLane:" + qid(lanes.track))->property("text") == "Filter Cutoff";
    }));
    CHECK(rise(200, 400) == Approx(rise(2000, 4000)).margin(0.5));
    CHECK(lanes.call("valueAt", lanes.call("yOf", 1000)) == Approx(1000).epsilon(0.01));
    MainWindow::settle();
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

TEST_CASE("an added lane draws its line in its own row below the track, and its hide button removes the row") {
    Lanes lanes;
    lanes.main.rig.run("automation.addPoint", {{"track", lanes.track}, {"param", "pan"}, {"tick", 0}, {"value", -1.0}});
    lanes.main.rig.run("automation.addPoint",
                       {{"track", lanes.track}, {"param", "pan"}, {"tick", 3840}, {"value", 1.0}});
    lanes.main.rig.run("automation.showLane", {{"track", lanes.track}, {"param", "pan"}, {"add", true}});
    QQuickItem* pan = lanes.main.item("automation.lane:" + qid(lanes.track) + ":pan");
    const QRectF first = lanes.lane()->mapRectToScene(lanes.lane()->boundingRect());
    const QRectF below = pan->mapRectToScene(pan->boundingRect());
    CHECK(below.top() == Approx(first.bottom()));
    CHECK(below.height() == Approx(first.height()));
    CHECK(lanes.main.item("automation.showLane:" + qid(lanes.track) + ":pan")->property("text") == "Pan");

    const QImage image = lanes.main.window->grabWindow();
    const QColor color = pan->property("trackColor").value<QColor>();
    const double ratio = image.devicePixelRatio();
    int drawn = 0;
    for (int y = static_cast<int>(below.top() * ratio); y < static_cast<int>(below.bottom() * ratio); ++y) {
        for (int x = static_cast<int>(below.left() * ratio); x < static_cast<int>(below.right() * ratio); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            drawn += std::abs(pixel.red() - color.red()) + std::abs(pixel.green() - color.green()) +
                                 std::abs(pixel.blue() - color.blue()) <
                             24
                         ? 1
                         : 0;
        }
    }
    CHECK(drawn > static_cast<int>(below.width()));

    lanes.main.click(lanes.main.item("automation.hideLane:" + qid(lanes.track) + ":pan"));
    CHECK(lanes.main.rig.nav.shownLanes(qid(lanes.track)) == QStringList{"gain"});
    CHECK(processUntil([&] {
        return MainWindow::find(lanes.main.window->contentItem(), "automation.lane:" + qid(lanes.track) + ":pan") ==
               nullptr;
    }));
    MainWindow::settle();
}

TEST_CASE("in Read the mixer fader and the header gain set the gain and add no lane, and in Touch both write one") {
    MainWindow& main = MainWindow::instance();
    main.rig.nav.setAutomationOpen(false);
    const std::string track = onlyTrack(main);
    main.rig.nav.setTrackHeight(kMaxTrackHeight);
    main.rig.nav.setMixerOpen(true);
    MainWindow::settle();
    const auto& edited = *main.rig.controller->project().track(model::TrackId{track});
    const auto handleOf = [&](const char* view) {
        QQuickItem* found = nullptr;
        processUntil([&] {
            found = MainWindow::find(main.item(view), "track.setGain:" + qid(track));
            return found != nullptr;
        });
        REQUIRE(found != nullptr);
        return found->property("handle").value<QQuickItem*>();
    };
    const auto dragBoth = [&] {
        double gain = edited.gainDb;
        main.drag(handleOf("mixer"), {0, -30});
        CHECK(edited.gainDb != gain);
        gain = edited.gainDb;
        main.drag(handleOf("tracks"), {-30, 0});
        CHECK(edited.gainDb != gain);
    };

    for (const bool playing : {false, true}) {
        if (playing) {
            main.rig.run("transport.play");
        }
        dragBoth();
        CHECK(edited.automation.empty());
        CHECK_FALSE(main.rig.nav.automationOpen());
        CHECK_FALSE(main.rig.nav.automationLanes().contains(qid(track)));
    }

    main.rig.run("automation.setMode", {{"track", track}, {"mode", "touch"}});
    MainWindow::settle();
    main.drag(handleOf("mixer"), {0, -30});
    CHECK(model::findLane(edited, "gain") != nullptr);
    main.rig.run("automation.clearLane", {{"track", track}, {"param", "gain"}});
    const double gain = edited.gainDb;
    main.drag(handleOf("tracks"), {-30, 0});
    CHECK(model::findLane(edited, "gain") != nullptr);
    CHECK(edited.gainDb == gain);
    main.rig.run("transport.stop");
    main.rig.nav.setMixerOpen(false);
    MainWindow::settle();
}

TEST_CASE("dragging a track header lands on the row under the pointer below a track with lanes open") {
    Lanes lanes;
    const std::string second = lanes.main.rig.run("track.addAudio").at("id");
    const std::string third = lanes.main.rig.run("track.addAudio").at("id");
    lanes.main.rig.run("automation.showLane", {{"track", lanes.track}, {"param", "pan"}, {"add", true}});
    MainWindow::settle();
    QQuickItem* from = lanes.main.item("track.move:" + qid(lanes.track));
    QQuickItem* onto = lanes.main.item("track.move:" + qid(second));
    const QPointF offset = onto->mapToScene({onto->width() / 2, onto->height() / 2}) -
                           from->mapToScene({from->width() / 2, from->height() / 2});
    lanes.main.drag(from, offset);
    std::vector<std::string> order;
    for (const model::Track& track : lanes.main.rig.controller->project().document().tracks) {
        order.push_back(track.id.value);
    }
    CHECK(order == std::vector{second, lanes.track, third});
    lanes.main.rig.run("automation.hideLane", {{"track", lanes.track}, {"param", "pan"}});
    MainWindow::settle();
}
