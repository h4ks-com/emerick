#include "emerick/core/time.hpp"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "emerick/core/id.hpp"

using namespace emerick::core;

TEST_CASE("bar.beat.tick round trips through ticks in 4/4") {
    const TempoMap map{96.0, {4, 4}};
    const BarBeatTick position{17, 4, 120};
    CHECK(map.toBarBeatTick(map.fromBarBeatTick(position)) == position);
    CHECK(map.fromBarBeatTick({2, 1, 0}) == 4 * kTicksPerQuarter);
}

TEST_CASE("ticks convert to seconds and frames at the project tempo") {
    const TempoMap map{96.0, {4, 4}};
    CHECK(map.ticksToSeconds(kTicksPerQuarter) == 0.625);
    CHECK(map.ticksToFrames(map.ticksPerBar(), 48000.0) == 120000);
    CHECK(map.framesToTicks(120000, 48000.0) == map.ticksPerBar());
}

TEST_CASE("bar.beat.tick parses and formats") {
    CHECK(BarBeatTick::parse("40.2.188") == BarBeatTick{40, 2, 188});
    CHECK(BarBeatTick::parse("5") == BarBeatTick{5, 1, 0});
    CHECK(BarBeatTick{3, 2, 1}.format() == "3.2.1");
    CHECK_THROWS_AS(BarBeatTick::parse("0.1.0"), std::invalid_argument);
}

TEST_CASE("ids are unique and typed") {
    struct TrackTag {};
    const auto first = Id<TrackTag>::make();
    const auto second = Id<TrackTag>::make();
    CHECK(first.value.size() == 12);
    CHECK(first != second);
}
