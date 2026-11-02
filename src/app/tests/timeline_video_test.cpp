#include "emerick/app/timeline_video.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace emerick;
using namespace emerick::app;

namespace {

core::TempoMap tempoAt(double bpm) {
    core::TempoMap tempo;
    tempo.bpm = bpm;
    return tempo;
}

}  // namespace

TEST_CASE("a video has a frame for every started frame period of its range") {
    const core::TempoMap tempo = tempoAt(120);
    const core::Ticks bar = tempo.ticksPerBar();
    CHECK(videoFrameCount(tempo, 0, bar, 30) == 60);
    CHECK(videoFrameCount(tempo, 0, bar, 10) == 20);
    CHECK(videoFrameCount(tempo, bar, 2 * bar, 30) == 60);
    CHECK(videoFrameCount(tempo, 0, tempo.secondsToTicks(2.01), 10) == 21);
}

TEST_CASE("frame n shows the tick n over fps seconds after the start") {
    const core::TempoMap tempo = tempoAt(90);
    const core::Ticks from = tempo.ticksPerBar();
    CHECK(videoFrameTicks(tempo, from, 0, 30) == from);
    for (const int frame : {1, 29, 30, 451}) {
        CHECK(videoFrameTicks(tempo, from, frame, 30) ==
              tempo.secondsToTicks(tempo.ticksToSeconds(from) + (frame / 30.0)));
    }
}

TEST_CASE("the playhead walks in from the left and then holds while the lanes scroll") {
    constexpr double kVisible = 1000;
    CHECK(videoScrollTicks(0, 0, kVisible) == 0);
    CHECK(videoScrollTicks(299, 0, kVisible) == 0);
    CHECK(videoScrollTicks(300, 0, kVisible) == Catch::Approx(0).margin(1e-9));
    CHECK(videoScrollTicks(800, 0, kVisible) == Catch::Approx(500));
    CHECK(videoScrollTicks(2000, 1900, kVisible) == 1900);
    CHECK(videoScrollTicks(2500, 1900, kVisible) == Catch::Approx(2200));
}
