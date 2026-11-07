#include "emerick/engine/pitch.hpp"

#include <algorithm>
#include <bit>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <complex>
#include <cstddef>
#include <functional>
#include <iterator>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "emerick/engine/pitch_needle.hpp"

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

// A plucked guitar string: stretched partials with a weak, fast fading fundamental, a body resonance, a noisy pick
// attack and background noise.
std::vector<float> pluck(double hz, double rate, double pluckPosition = 0.2, double noiseLevel = 0.01) {
    constexpr double kSeconds = 2.5;
    constexpr double kStretch = 1e-4;
    const auto size = static_cast<std::size_t>(rate * kSeconds);
    unsigned state = 7;
    auto noise = [&state] {
        state = (state * 1103515245U) + 12345U;
        return static_cast<double>((state >> 8U) % 2001) / 1000.0 - 1.0;
    };
    std::vector<std::pair<std::complex<double>, std::complex<double>>> partials;
    for (int n = 1; n * hz < rate / 2.2 && n < 60; ++n) {
        const double partialHz = n * hz * std::sqrt(1 + (kStretch * n * n));
        const double body = 1 + std::exp(-std::pow(std::log(partialHz / 100) / 0.15, 2));
        const double level = std::abs(std::sin(n * std::numbers::pi * pluckPosition)) / n * body * (n == 1 ? 0.3 : 1.0);
        const double decay = 0.6 + (0.004 * partialHz) + (n == 1 ? 1.5 : 0.0);
        partials.emplace_back(std::polar(level, std::numbers::pi * noise()),
                              std::polar(std::exp(-decay / rate), 2 * std::numbers::pi * partialHz / rate));
    }
    std::vector<float> samples(size);
    for (std::size_t i = 0; i < size; ++i) {
        const double t = static_cast<double>(i) / rate;
        double sum = 0;
        for (auto& [value, step] : partials) {
            sum += value.imag();
            value *= step;
        }
        sum *= std::min(t / 0.004, 1.0);
        sum += (0.5 * noise() * std::exp(-t / 0.008)) + (noiseLevel * noise());
        samples[i] = static_cast<float>(0.4 * sum);
    }
    return samples;
}

// Feeds every reading the tuner takes of the samples to a needle and returns the note it shows after each one, or -1.
std::vector<int> shownNotes(std::span<const float> samples, double rate) {
    const std::size_t frame = std::bit_ceil(static_cast<std::size_t>(rate * 0.064));
    const std::size_t hop = frame / 4;
    PitchNeedle needle;
    std::vector<int> notes;
    for (std::size_t end = frame; end <= samples.size(); end += hop) {
        needle.feed(detectPitch(samples.subspan(end - frame, frame), rate), static_cast<double>(hop) / rate, 440.0);
        notes.push_back(needle.shown() ? static_cast<int>(std::lround(needle.semitones())) : -1);
    }
    return notes;
}

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

TEST_CASE("a plucked guitar string reads its own note from the attack to the fade") {
    const double hz =
        GENERATE(82.41, 98.0, 110.0, 130.81, 138.59, 146.83, 155.56, 196.0, 246.94, 311.13, 329.63, 349.23, 659.26);
    const double rate = GENERATE(44100.0, 48000.0);
    CAPTURE(hz, rate);
    const std::vector<float> samples = pluck(hz, rate);
    const std::size_t frame = std::bit_ceil(static_cast<std::size_t>(rate * 0.064));
    const int expected = nearestNote(hz, 440.0).midiNote;
    for (std::size_t end = frame; end <= samples.size(); end += frame) {
        const double seconds = static_cast<double>(end) / rate;
        const std::optional<PitchReading> reading = detectPitch(std::span(samples).subspan(end - frame, frame), rate);
        if (!reading || seconds < 0.1) {
            continue;
        }
        CAPTURE(seconds, reading->hz);
        CHECK(nearestNote(reading->hz, 440.0).midiNote == expected);
        if (seconds > 0.3) {
            CHECK(std::abs(centsBetween(reading->hz, hz)) < 8.0);
        }
    }
}

TEST_CASE("the needle keeps a plucked string's note when its fading fundamental lets the reading jump to an overtone") {
    struct Pluck {
        double hz;
        double pluckPosition;
    };
    const Pluck string = GENERATE(Pluck{82.41, 0.5}, Pluck{82.41, 0.33}, Pluck{110.0, 0.25}, Pluck{110.0, 0.33},
                                  Pluck{146.83, 0.33}, Pluck{164.81, 0.33});
    CAPTURE(string.hz, string.pluckPosition);
    const std::vector<float> samples = pluck(string.hz, kRate, string.pluckPosition, 0.003);
    const int expected = nearestNote(string.hz, 440.0).midiNote;
    const std::vector<int> notes = shownNotes(samples, kRate);
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (notes[i] != -1) {
            CAPTURE(i);
            CHECK(notes[i] == expected);
        }
    }
}

TEST_CASE("the needle moves to a fresh pluck within 150 ms, even an octave up") {
    const double next = GENERATE(110.0, 164.81, 220.0, 247.22);
    CAPTURE(next);
    std::vector<float> samples = pluck(110.0, kRate, 0.33, 0.003);
    samples.resize(static_cast<std::size_t>(kRate * 1.5));
    const std::size_t plucked = samples.size();
    std::ranges::copy(pluck(next, kRate), std::back_inserter(samples));
    const std::vector<int> notes = shownNotes(samples, kRate);
    const std::size_t hop = 1024;
    const std::size_t settled = ((plucked + static_cast<std::size_t>(kRate * 0.15)) / hop) - 3;
    CHECK(notes[(plucked / hop) - 4] == 45);
    for (std::size_t i = settled; i < notes.size(); ++i) {
        CAPTURE(i);
        CHECK(notes[i] == nearestNote(next, 440.0).midiNote);
    }
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
