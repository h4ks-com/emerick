#include "emerick/app/control_curve.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace emerick::app;

TEST_CASE("a controller lane dots only the points that stand apart, so a dense bend reads as a line") {
    const std::vector<QPointF> sparse{{0, 10}, {40, 20}, {80, 30}};
    CHECK(spacedDots(sparse) == sparse);

    std::vector<QPointF> bend;
    for (int x = 100; x < 200; x += 2) {
        bend.emplace_back(x, x / 4.0);
    }
    bend.emplace_back(300, 5);
    CHECK(spacedDots(bend) == std::vector<QPointF>{{300, 5}});

    const std::vector<QPointF> pair{{0, 0}, {4, 0}, {60, 0}};
    CHECK(spacedDots(pair) == std::vector<QPointF>{{60, 0}});
    CHECK(spacedDots({}).empty());
}
