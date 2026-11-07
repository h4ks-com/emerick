#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <ranges>
#include <string>

#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

QString qid(const std::string& id) { return QString::fromStdString(id); }

const model::Track& trackOf(const MainWindow& main, const std::string& id) {
    return *main.rig.controller->project().track(model::TrackId{id});
}

double paramOf(const MainWindow& main, const std::string& track, const std::string& name) {
    return trackOf(main, track).inserts.front().params.at(name);
}

}  // namespace

TEST_CASE("a mixer strip adds a Channel EQ from its empty slot, and dragging a band point runs live in one undo step") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    main.rig.nav.setMixerOpen(true);
    MainWindow::settle();

    QQuickItem* slot = main.item("insert.add:" + qid(vox));
    main.click(slot);
    auto* picker = slot->property("menu").value<QObject*>();
    REQUIRE(picker != nullptr);
    REQUIRE(processUntil([&] { return picker->property("opened").toBool(); }));
    QMetaObject::invokeMethod(menuItem(picker, "Channel EQ")->property("action").value<QObject*>(), "trigger");
    REQUIRE(processUntil([&] { return !picker->property("visible").toBool(); }));
    REQUIRE(trackOf(main, vox).inserts.size() == 1);
    const std::string eq = trackOf(main, vox).inserts.front().id.value;
    CHECK(main.rig.nav.editingInsert().toStdString() == eq);
    MainWindow::settle();
    CHECK(main.item("insert.edit:" + qid(eq))->property("text").toString() == "Channel EQ");
    CHECK(main.item("eqGraph.level:0")->property("text").toString() == "0");

    const auto undoBefore = main.rig.controller->project().undoName();
    QQuickItem* point = main.item("insert.setParam:" + qid(eq) + ":peak2");
    const QPointF press = point->mapToScene({point->width() / 2, point->height() / 2});
    main.send(QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::NoModifier);
    for (int step = 1; step <= 5; ++step) {
        main.send(QEvent::MouseMove, press + QPointF{8.0 * step, -8.0 * step}, Qt::LeftButton, Qt::NoModifier);
    }
    CHECK(processUntil([&] { return paramOf(main, vox, "peak2Gain") > 0.0; }));
    CHECK(paramOf(main, vox, "peak2Freq") > 1000.0);
    main.send(QEvent::MouseButtonRelease, press + QPointF{40, -40}, Qt::NoButton, Qt::NoModifier);
    MainWindow::settle();
    CHECK(paramOf(main, vox, "peak2Gain") > 3.0);

    main.rig.run("project.undo");
    CHECK(paramOf(main, vox, "peak2Gain") == 0.0);
    CHECK(paramOf(main, vox, "peak2Freq") == 1000.0);
    CHECK(main.rig.controller->project().undoName() == undoBefore);

    main.click(main.item("insert.closeEditor"));
    CHECK(processUntil([&] { return main.rig.nav.editingInsert().isEmpty(); }));
    main.rig.nav.setMixerOpen(false);
    MainWindow::settle();
}

TEST_CASE("an insert slot bypasses from its power button, removes from its remove button, and reorders by drag") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    const std::string delay = main.rig.run("insert.add", {{"track", vox}, {"type", "delay"}}).at("id");
    const std::string eq = main.rig.run("insert.add", {{"track", vox}, {"type", "eq"}}).at("id");
    const std::string verb = main.rig.run("insert.add", {{"track", vox}, {"type", "reverb"}}).at("id");
    main.rig.run("insert.closeEditor");
    MainWindow::settle();

    main.click(main.item("insert.setBypass:" + qid(delay) + ":slot"));
    CHECK(processUntil([&] { return trackOf(main, vox).inserts.front().bypass; }));

    main.click(main.item("insert.remove:" + qid(eq) + ":slot"));
    REQUIRE(processUntil([&] { return trackOf(main, vox).inserts.size() == 2; }));
    CHECK(trackOf(main, vox).inserts[1].id.value == verb);
    MainWindow::settle();

    QQuickItem* first = main.item("insert.edit:" + qid(delay));
    QQuickItem* last = main.item("insert.edit:" + qid(verb));
    const double rows = last->mapToScene({0, 0}).y() - first->mapToScene({0, 0}).y();
    main.drag(last, QPointF{0, -rows});
    CHECK(processUntil([&] { return trackOf(main, vox).inserts.front().id.value == verb; }));
    MainWindow::settle();
}

TEST_CASE("clicking the gap between two insert slots adds the picked effect there") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    main.rig.run("insert.add", {{"track", vox}, {"type", "delay"}});
    main.rig.run("insert.add", {{"track", vox}, {"type", "reverb"}});
    main.rig.run("insert.closeEditor");
    MainWindow::settle();

    QQuickItem* gap = main.item("insert.add:" + qid(vox) + ":at:1");
    main.hover(gap);
    CHECK(processUntil([&] { return gap->property("containsMouse").toBool(); }));
    main.click(gap);
    auto* picker = main.item("insert.add:" + qid(vox))->property("menu").value<QObject*>();
    REQUIRE(processUntil([&] { return picker->property("opened").toBool(); }));
    QMetaObject::invokeMethod(menuItem(picker, "Channel EQ")->property("action").value<QObject*>(), "trigger");
    REQUIRE(processUntil([&] { return !picker->property("visible").toBool(); }));
    REQUIRE(trackOf(main, vox).inserts.size() == 3);
    CHECK(trackOf(main, vox).inserts[1].type == "eq");
    main.rig.run("insert.closeEditor");
    MainWindow::settle();
}

TEST_CASE("an insert slot hides its tooltip once a drag starts") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    const std::string delay = main.rig.run("insert.add", {{"track", vox}, {"type", "delay"}}).at("id");
    main.rig.run("insert.add", {{"track", vox}, {"type", "reverb"}});
    main.rig.run("insert.closeEditor");
    MainWindow::settle();

    QQuickItem* slot = main.item("insert.edit:" + qid(delay));
    CHECK_FALSE(slot->property("tipDismissed").toBool());
    const QPointF from = slot->mapToScene({slot->width() / 2, slot->height() / 2});
    main.send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::NoModifier);
    main.send(QEvent::MouseMove, from + QPointF{0, 8}, Qt::LeftButton, Qt::NoModifier);
    CHECK(slot->property("tipDismissed").toBool());
    main.send(QEvent::MouseButtonRelease, from + QPointF{0, 8}, Qt::NoButton, Qt::NoModifier);
    MainWindow::settle();
    CHECK(slot->property("tipDismissed").toBool());
}

TEST_CASE("the insert editor's title bar shows an open hand and moves the editor while dragged") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    main.rig.run("insert.add", {{"track", vox}, {"type", "reverb"}});
    MainWindow::settle();

    QQuickItem* title = main.item("insertEditor.titleBar");
    const auto cursors = title->findChildren<QObject*>() | std::views::transform([](QObject* handler) {
                             return handler->property("cursorShape").toInt();
                         });
    CHECK(std::ranges::contains(cursors, Qt::OpenHandCursor));
    const QPointF before = title->mapToScene({0, 0});
    main.drag(title, QPointF{-60, -40});
    const QPointF moved = main.item("insertEditor.titleBar")->mapToScene({0, 0}) - before;
    CHECK(moved.x() == Catch::Approx(-60).margin(2));
    CHECK(moved.y() == Catch::Approx(-40).margin(2));
    main.rig.run("insert.closeEditor");
    MainWindow::settle();
}

TEST_CASE("a delay knob turns its param live, and a double click sets its default") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    const std::string delay = main.rig.run("insert.add", {{"track", vox}, {"type", "delay"}}).at("id");
    MainWindow::settle();

    QQuickItem* knob = main.item("insert.setParam:" + qid(delay) + ":feedback:knob");
    main.drag(knob, {0, -40});
    CHECK(paramOf(main, vox, "feedback") > 30.0);
    const QPointF center = knob->mapToScene({knob->width() / 2, knob->height() / 2});
    QTest::mouseDClick(main.window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    CHECK(processUntil([&] { return paramOf(main, vox, "feedback") == 30.0; }));
    main.rig.run("insert.closeEditor");
    MainWindow::settle();
}
