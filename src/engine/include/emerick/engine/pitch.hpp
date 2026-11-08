#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace emerick::engine {

inline constexpr double kLowestPitchHz = 30.0;
inline constexpr double kHighestPitchHz = 2000.0;
// A tuner measures a frame every hop, and its frames overlap by this many hops.
inline constexpr std::size_t kPitchHopsPerFrame = 4;

struct PitchReading {
    double hz = 0.0;
    // How periodic the frame is, from 0 for noise to 1 for a steady tone.
    double confidence = 0.0;
    double meanSquare = 0.0;
};

// Finds the fundamental of the frame with the McLeod pitch method, from kLowestPitchHz to kHighestPitchHz, or nullopt
// when the frame holds no periodic sound. The frame needs two periods of the lowest pitch it should find.
std::optional<PitchReading> detectPitch(std::span<const float> frame, double sampleRate);

struct NoteReading {
    int midiNote = 0;
    // How far the pitch sits from the note, from -50 to 50.
    double cents = 0.0;
};

// The nearest equal tempered note to hz with A4 at referenceHz.
NoteReading nearestNote(double hz, double referenceHz);
// The note's name without its octave, with sharps, such as C# for MIDI note 61.
std::string pitchClassName(int midiNote);
// The octave as note names count it, 4 for MIDI note 60.
int noteOctave(int midiNote);
// The note's name with its octave, C4 for MIDI note 60.
std::string noteName(int midiNote);

}  // namespace emerick::engine
