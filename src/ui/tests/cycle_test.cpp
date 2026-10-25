#include <QQuickItem>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

const model::Cycle& cycleOf(const MainWindow& main) { return main.rig.controller->project().document().cycle; }

}  // namespace

TEST_CASE(
    "the ruler's upper half sets the cycle on the grid, its bar toggles on a click and moves on a drag, and its "
    "edges resize it") {
    MainWindow& main = MainWindow::instance();
    const core::Ticks bar = main.rig.controller->ticksPerBar();
    QQuickItem* strip = main.item("cycle.set");
    QQuickItem* ruler = strip->parentItem();
    main.window->grabWindow();
    const double pixelsPerTick = ruler->property("pixelsPerTick").toDouble();
    const double scrollTicks = ruler->property("scrollTicks").toDouble();
    const auto at = [&](core::Ticks ticks) {
        return strip->mapToScene({(static_cast<double>(ticks) - scrollTicks) * pixelsPerTick, strip->height() / 2});
    };

    main.send(QEvent::MouseButtonPress, at(bar + 30), Qt::LeftButton, Qt::NoModifier);
    for (const core::Ticks ticks : {bar + 400, 2 * bar, (3 * bar) - 30}) {
        main.send(QEvent::MouseMove, at(ticks), Qt::LeftButton, Qt::NoModifier);
    }
    main.send(QEvent::MouseButtonRelease, at((3 * bar) - 30), Qt::NoButton, Qt::NoModifier);
    MainWindow::settle();
    CHECK(cycleOf(main) == model::Cycle{.enabled = true, .start = bar, .end = 3 * bar});
    CHECK(main.rig.controller->project().undoName().value_or("") == "set cycle");

    QQuickItem* body = MainWindow::find(ruler, "cycle.toggle");
    REQUIRE(body != nullptr);
    main.click(body);
    CHECK_FALSE(cycleOf(main).enabled);
    QTest::keyClick(main.window, Qt::Key_C);
    CHECK(cycleOf(main).enabled);
    main.click(main.item("cycle.toggle"));
    CHECK_FALSE(cycleOf(main).enabled);

    main.drag(body, QPointF{static_cast<double>(bar) * pixelsPerTick, 0});
    CHECK(cycleOf(main).start == 2 * bar);
    CHECK(cycleOf(main).end == 4 * bar);

    main.drag(MainWindow::find(ruler, "cycle.set:end"), QPointF{static_cast<double>(bar) * pixelsPerTick, 0});
    CHECK(cycleOf(main).end == 5 * bar);
    main.drag(MainWindow::find(ruler, "cycle.set:start"), QPointF{-static_cast<double>(bar) * pixelsPerTick, 0});
    CHECK(cycleOf(main) == model::Cycle{.enabled = false, .start = bar, .end = 5 * bar});
}
