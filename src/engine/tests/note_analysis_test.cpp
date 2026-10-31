#include "emerick/engine/note_analysis.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <random>
#include <vector>

using Catch::Approx;
using emerick::engine::analyzeNote;
using emerick::engine::noteHz;

namespace {

constexpr double kRate = 48000;
constexpr std::size_t kFrames = 48000;

std::vector<float> harmonicTone(double hz) {
    std::vector<float> samples(kFrames);
    for (std::size_t i = 0; i < kFrames; ++i) {
        double sample = 0;
        for (int harmonic = 1; harmonic <= 6; ++harmonic) {
            sample += 0.4 / harmonic * std::sin(2 * std::numbers::pi * hz * harmonic * static_cast<double>(i) / kRate);
        }
        samples[i] = static_cast<float>(sample);
    }
    return samples;
}

}  // namespace

TEST_CASE("note 69 is A at 440 Hz and an octave doubles it") {
    CHECK(noteHz(69) == Approx(440));
    CHECK(noteHz(81) == Approx(880));
}

TEST_CASE("a note made of its harmonics reads almost all harmonic, and white noise does not") {
    const auto tone = analyzeNote({harmonicTone(noteHz(57))}, kRate, noteHz(57));
    CHECK(tone.harmonicShare > 0.98);
    CHECK(tone.peakDb < 0);
    CHECK(tone.centroidHz > noteHz(57));

    std::mt19937 random{7};
    std::uniform_real_distribution<float> noise{-0.5F, 0.5F};
    std::vector<float> hiss(kFrames);
    for (float& sample : hiss) {
        sample = noise(random);
    }
    CHECK(analyzeNote({hiss}, kRate, noteHz(57)).harmonicShare < 0.4);

    std::vector<float> mixed = harmonicTone(noteHz(57));
    for (std::size_t i = 0; i < kFrames; ++i) {
        mixed[i] += hiss[i];
    }
    const double dirty = analyzeNote({mixed}, kRate, noteHz(57)).harmonicShare;
    CHECK(dirty < tone.harmonicShare - 0.1);
}

TEST_CASE("a silent note reads minus infinity and no harmonic share") {
    const auto silent = analyzeNote({std::vector<float>(kFrames)}, kRate, 440);
    CHECK(std::isinf(silent.peakDb));
    CHECK(silent.harmonicShare == 0);
}
