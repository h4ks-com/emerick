#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>

#include "emerick/core/time.hpp"

namespace emerick::engine {

inline constexpr double kNsPerSecond = 1e9;

inline std::int64_t steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline std::int64_t framesToNs(core::Frames frames, double sampleRate) {
    return std::llround(static_cast<double>(frames) * kNsPerSecond / sampleRate);
}

inline core::Frames nsToFrames(std::int64_t ns, double sampleRate) {
    return std::llround(static_cast<double>(ns) * sampleRate / kNsPerSecond);
}

}  // namespace emerick::engine
