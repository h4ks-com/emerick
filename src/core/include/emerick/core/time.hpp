#pragma once

#include <cstdint>
#include <string>

namespace emerick::core {

using Ticks = std::int64_t;
using Frames = std::int64_t;

inline constexpr Ticks kTicksPerQuarter = 960;

struct Meter {
    int numerator = 4;
    int denominator = 4;

    bool operator==(const Meter&) const = default;
};

// Bars and beats count from 1, ticks from 0, as musicians and Logic write them.
struct BarBeatTick {
    int bar = 1;
    int beat = 1;
    int tick = 0;

    [[nodiscard]] std::string format() const;
    static BarBeatTick parse(const std::string& text);
    auto operator<=>(const BarBeatTick&) const = default;
};

struct TempoMap {
    double bpm = 120.0;
    Meter meter{};

    [[nodiscard]] Ticks ticksPerBeat() const;
    [[nodiscard]] Ticks ticksPerBar() const;
    [[nodiscard]] double ticksToSeconds(Ticks ticks) const;
    [[nodiscard]] Ticks secondsToTicks(double seconds) const;
    [[nodiscard]] Frames ticksToFrames(Ticks ticks, double sampleRate) const;
    [[nodiscard]] Ticks framesToTicks(Frames frames, double sampleRate) const;
    [[nodiscard]] BarBeatTick toBarBeatTick(Ticks ticks) const;
    [[nodiscard]] Ticks fromBarBeatTick(const BarBeatTick& position) const;
    // A length counts whole bars and beats from zero, so two beats read "0.2.0".
    [[nodiscard]] std::string durationText(Ticks ticks) const;
    bool operator==(const TempoMap&) const = default;
};

}  // namespace emerick::core
