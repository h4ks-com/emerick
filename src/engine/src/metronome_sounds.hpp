#pragma once

#include <array>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/engine/session.hpp"

namespace emerick::engine {

// One mono one-shot per drum voice, indexed by DrumVoice.
struct DrumSounds {
    std::array<std::vector<float>, kDrumVoices> voices;
};

// We synthesize every voice, so the metronome needs no audio files: wood block blips for the click, a pitch swept
// kick, a noisy snare and a short hat.
DrumSounds synthesizeDrums(double sampleRate);

// Reads the first seconds of a sound file as mono at sampleRate.
std::expected<std::vector<float>, std::string> loadOneShot(const std::filesystem::path& path, double sampleRate);

// One bar of a groove over four beats, a step per character: X a full hit, x a soft one, . a rest.
struct Groove {
    int stepsPerBeat = 4;
    std::array<std::string_view, 3> voices;  // kick, snare, hat
};

inline constexpr int kGrooveBeats = 4;

// Indexed by GroovePattern.
inline constexpr std::array kGrooves{
    Groove{4, {"X.......X.x.....", "....X.......X...", "X.x.X.x.X.x.X.x."}},
    Groove{4, {"X...X...X...X...", "....X.......X...", "..X...X...X...X."}},
    Groove{4, {"X.........x.....", "........X.......", "X.x.X.x.X.x.X.x."}},
    Groove{3, {"X.....X.....", "...X.....X..", "X.xX.xX.xX.x"}},
    Groove{4, {"X..xX..xX..xX..x", "X..X..X...X..X..", "X.x.X.x.X.x.X.x."}},
};

}  // namespace emerick::engine
