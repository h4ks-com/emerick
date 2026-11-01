#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

#include "emerick/engine/audio_analysis.hpp"

namespace emerick::engine {

inline constexpr double kSilentDb = -std::numeric_limits<double>::infinity();
inline constexpr double kStepSeconds = 0.1;
inline constexpr float kClipLevel = 0.999F;
inline constexpr std::size_t kToneFrame = 4096;
inline constexpr double kActiveGateDb = -60;

struct Biquad {
    double b0 = 1;
    double b1 = 0;
    double b2 = 0;
    double a1 = 0;
    double a2 = 0;
    double z1 = 0;
    double z2 = 0;

    double process(double x) {
        const double y = (b0 * x) + z1;
        z1 = (b1 * x) - (a1 * y) + z2;
        z2 = (b2 * x) - (a2 * y);
        return y;
    }
};

inline Biquad butterworth(double hz, double sampleRate, bool highPass) {
    const double w0 = 2 * std::numbers::pi * hz / sampleRate;
    const double cosine = std::cos(w0);
    const double alpha = std::sin(w0) / std::numbers::sqrt2;
    const double a0 = 1 + alpha;
    const double edge = highPass ? (1 + cosine) / 2 : (1 - cosine) / 2;
    return {.b0 = edge / a0,
            .b1 = (highPass ? -2 * edge : 2 * edge) / a0,
            .b2 = edge / a0,
            .a1 = -2 * cosine / a0,
            .a2 = (1 - alpha) / a0};
}

inline double powerDb(double meanSquare) { return 10 * std::log10(meanSquare); }

inline double amplitudeDb(double amplitude) { return 20 * std::log10(amplitude); }

inline std::size_t stepFramesAt(double sampleRate) {
    return static_cast<std::size_t>(std::lround(kStepSeconds * sampleRate));
}

// The upper of the two middle values for an even count.
inline double median(std::vector<double> values) {
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::ranges::nth_element(values, middle);
    return *middle;
}

// The median level of the steps of stepFrames frames above kActiveGateDb, from each step's sum of squares over every
// channel, the last step possibly short, or minus infinity when none is that loud.
double activeRmsDb(std::span<const double> stepSquares, std::size_t frames, std::size_t stepFrames,
                   std::size_t channels);

// Sets the band shares and the centroid of tone from a power spectrum of kToneFrame-sample frames, and leaves them 0
// when the spectrum is silent.
void setSpectrumTone(ToneBalance& tone, std::span<const double> power, double sampleRate);

}  // namespace emerick::engine
