#include "emerick/model/automation.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <utility>

#include "emerick/model/json.hpp"

using namespace emerick::model;
using Catch::Approx;
using emerick::core::AutomationCurve;

namespace {

AutomationLane gainLane() {
    return {.param = "gain",
            .points = {{.tick = 0, .value = -12.0, .curve = AutomationCurve::linear},
                       {.tick = 960, .value = 0.0, .curve = AutomationCurve::hold},
                       {.tick = 1920, .value = -6.0, .curve = AutomationCurve::smooth},
                       {.tick = 2880, .value = 6.0, .curve = AutomationCurve::linear}}};
}

}  // namespace

TEST_CASE("a lane holds its first value before it, its last after it and follows each segment's curve between") {
    const AutomationLane lane = gainLane();
    const auto at = [&](emerick::core::Ticks tick) { return automationValueAt(lane, tick).value(); };
    CHECK(at(-100) == -12.0);
    CHECK(at(480) == Approx(-6.0));
    CHECK(at(1919) == 0.0);
    CHECK(at(1920) == -6.0);
    CHECK(at(2400) == Approx(0.0).margin(1e-9));
    CHECK(at(2160) < -3.0);
    CHECK(at(9999) == 6.0);
    CHECK_FALSE(automationValueAt(AutomationLane{.param = "pan", .points = {}}, 0).has_value());
}

TEST_CASE("lanes and the automation mode round trip through json, sorted by tick") {
    Document document;
    Track track;
    track.automationMode = AutomationMode::latch;
    const AutomationLane sorted = gainLane();
    AutomationLane lane = sorted;
    std::swap(lane.points[0], lane.points[3]);
    track.automation = {lane, {.param = "send.bus-1.level", .points = {{.tick = 10, .value = -3.0}}}};
    document.tracks.push_back(track);

    const Document loaded = documentFromJson(toJson(document));
    CHECK(loaded.tracks[0].automationMode == AutomationMode::latch);
    REQUIRE(loaded.tracks[0].automation.size() == 2);
    CHECK(loaded.tracks[0].automation[0] == sorted);
    CHECK(loaded.tracks[0].automation[1] == document.tracks[0].automation[1]);
}

TEST_CASE("a format 5 project loads every track in Read with no lanes, and an unknown curve is refused") {
    Document document;
    document.tracks.emplace_back();
    auto json = toJson(document);
    json["format"] = 5;
    json["tracks"][0].erase("automation");
    json["tracks"][0].erase("automationMode");
    const Document loaded = documentFromJson(json);
    CHECK(loaded.tracks[0].automationMode == AutomationMode::read);
    CHECK(loaded.tracks[0].automation.empty());

    document.tracks[0].automation = {gainLane()};
    json = toJson(document);
    json["tracks"][0]["automation"][0]["points"][0]["curve"] = "wobbly";
    CHECK_THROWS_AS(documentFromJson(json), std::runtime_error);
}
