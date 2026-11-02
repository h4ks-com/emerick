#include "emerick/engine/pitch.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>
#include <optional>
#include <vector>

using Catch::Approx;
using namespace emerick::engine;

namespace {

constexpr double kRate = 48000.0;
constexpr std::size_t kFrame = 4096;

std::vector<float> tone(double hz, const std::function<double(double phase)>& shape) {
    std::vector<float> frame(kFrame);
    for (std::size_t i = 0; i < kFrame; ++i) {
        frame[i] = static_cast<float>(0.5 * shape(2 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
    }
    return frame;
}

// Every harmonic below Nyquist at 1/n, as a band limited sawtooth has them.
std::function<double(double)> sawtooth(double hz) {
    return [hz](double phase) {
        double sum = 0;
        for (int harmonic = 1; harmonic * hz < kRate / 2; ++harmonic) {
            sum += std::sin(harmonic * phase) / harmonic;
        }
        return sum * 2 / std::numbers::pi;
    };
}

double centsBetween(double hz, double expected) { return 1200 * std::log2(hz / expected); }

}  // namespace

TEST_CASE("a sine from the low E string to 1 kHz reads within a cent") {
    const double hz = GENERATE(82.41, 110.0, 146.83, 196.0, 261.63, 329.63, 440.0, 659.26, 1000.0);
    const std::optional<PitchReading> reading =
        detectPitch(tone(hz, [](double phase) { return std::sin(phase); }), kRate);
    REQUIRE(reading);
    CHECK(std::abs(centsBetween(reading->hz, hz)) < 1.0);
    CHECK(reading->confidence > 0.9);
}

TEST_CASE("a sawtooth full of harmonics reads its fundamental within a cent") {
    const double hz = GENERATE(82.41, 98.0, 123.47, 220.0, 293.66, 440.0, 587.33, 880.0, 1000.0);
    const std::optional<PitchReading> reading = detectPitch(tone(hz, sawtooth(hz)), kRate);
    REQUIRE(reading);
    CHECK(std::abs(centsBetween(reading->hz, hz)) < 1.0);
}

TEST_CASE("silence and noise hold no pitch") {
    CHECK_FALSE(detectPitch(std::vector<float>(kFrame), kRate));
    std::vector<float> noise(kFrame);
    unsigned state = 1;
    for (float& sample : noise) {
        state = (state * 1103515245U) + 12345U;
        sample = static_cast<float>(static_cast<int>(state >> 8U) % 2000 - 1000) / 2000.0F;
    }
    CHECK_FALSE(detectPitch(noise, kRate));
}

TEST_CASE("a pitch names its nearest note and how many cents it sits off") {
    const NoteReading a = nearestNote(440.0, 440.0);
    CHECK(a.midiNote == 69);
    CHECK(a.cents == Approx(0.0).margin(1e-9));
    const NoteReading sharpE = nearestNote(440.0 * std::pow(2.0, (-29.0 + 0.1) / 12), 440.0);
    CHECK(noteName(sharpE.midiNote) == "E2");
    CHECK(sharpE.cents == Approx(10.0).margin(0.05));
    CHECK(nearestNote(432.0, 432.0).cents == Approx(0.0).margin(1e-9));
    CHECK(noteName(60) == "C4");
    CHECK(noteName(61) == "C#4");
}
