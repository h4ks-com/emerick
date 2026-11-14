#pragma once

#include <cstdint>

namespace emerick::engine {

struct Biquad {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

struct BiquadState {
    double z1 = 0.0;
    double z2 = 0.0;
};

enum class FilterShape : std::uint8_t { lowPass, highPass, lowShelf, highShelf, peak };

// An RBJ cookbook filter; gainDb applies to the shelves and the peak.
Biquad design(FilterShape shape, double frequency, double gainDb, double q, double sampleRate);

inline double filterSample(const Biquad& filter, BiquadState& state, double input) {
    const double output = (filter.b0 * input) + state.z1;
    state.z1 = (filter.b1 * input) - (filter.a1 * output) + state.z2;
    state.z2 = (filter.b2 * input) - (filter.a2 * output);
    return output;
}

}  // namespace emerick::engine
