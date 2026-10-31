#pragma once

#include <array>
#include <cstddef>

namespace emerick::engine {

inline constexpr std::size_t kOversampling = 4;
inline constexpr std::size_t kTapsPerPhase = 12;
inline constexpr std::size_t kHalfTaps = kTapsPerPhase / 2;

using InterpolationTaps = std::array<std::array<double, kTapsPerPhase>, kOversampling>;

// Windowed sinc taps that interpolate three points between each pair of samples, after BS.1770 Annex 2. Phase p at
// frame f weights the sample at f + kHalfTaps - tap and lands at f + p / kOversampling.
InterpolationTaps interpolationTaps();

}  // namespace emerick::engine
