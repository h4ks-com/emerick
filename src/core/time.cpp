#include "emerick/core/time.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>

namespace emerick::core {

std::string BarBeatTick::format() const {
    return std::to_string(bar) + "." + std::to_string(beat) + "." + std::to_string(tick);
}

BarBeatTick BarBeatTick::parse(const std::string& text) {
    BarBeatTick position;
    const char* cursor = text.data();
    const char* const end = text.data() + text.size();
    int fields = 0;
    for (int* field : {&position.bar, &position.beat, &position.tick}) {
        const auto [next, error] = std::from_chars(cursor, end, *field);
        if (error != std::errc{}) {
            break;
        }
        ++fields;
        cursor = next;
        if (cursor == end || *cursor != '.') {
            break;
        }
        ++cursor;
    }
    if (fields < 1 || cursor != end || position.bar < 1 || position.beat < 1 || position.tick < 0) {
        throw std::invalid_argument("expected bar.beat.tick, got " + text);
    }
    return position;
}

Ticks TempoMap::ticksPerBeat() const { return kTicksPerQuarter * 4 / meter.denominator; }

Ticks TempoMap::ticksPerBar() const { return ticksPerBeat() * meter.numerator; }

double TempoMap::ticksToSeconds(Ticks ticks) const {
    return static_cast<double>(ticks) / static_cast<double>(kTicksPerQuarter) * 60.0 / bpm;
}

Ticks TempoMap::secondsToTicks(double seconds) const {
    return std::llround(seconds * bpm / 60.0 * static_cast<double>(kTicksPerQuarter));
}

Frames TempoMap::ticksToFrames(Ticks ticks, double sampleRate) const {
    return std::llround(ticksToSeconds(ticks) * sampleRate);
}

Ticks TempoMap::framesToTicks(Frames frames, double sampleRate) const {
    return secondsToTicks(static_cast<double>(frames) / sampleRate);
}

BarBeatTick TempoMap::toBarBeatTick(Ticks ticks) const {
    const Ticks bar = ticks / ticksPerBar();
    const Ticks inBar = ticks % ticksPerBar();
    return {static_cast<int>(bar + 1), static_cast<int>(inBar / ticksPerBeat() + 1),
            static_cast<int>(inBar % ticksPerBeat())};
}

Ticks TempoMap::fromBarBeatTick(const BarBeatTick& position) const {
    return (position.bar - 1) * ticksPerBar() + (position.beat - 1) * ticksPerBeat() + position.tick;
}

}  // namespace emerick::core
