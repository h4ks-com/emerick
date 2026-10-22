#pragma once

#include <vector>

#include "emerick/model/document.hpp"

namespace emerick::model {

inline constexpr int kMaxPitch = 127;
inline constexpr int kMaxVelocity = 127;

// Moves each note-on and its note-off from the clip's events into its notes; a note still held at the clip end lasts
// until the end.
void gatherNotes(MidiClip& clip);

// The notes as note-ons and note-offs merged in time order with the other events inside the clip, as playback and
// export send them. Every note ends by the clip end, and a note-off goes before a note-on at the same tick.
std::vector<MidiEvent> clipEvents(const MidiClip& clip);

// Moves at toward the nearest multiple of grid by strength, from 0 (unchanged) to 1 (on the grid).
Ticks quantized(Ticks at, Ticks grid, double strength);

}  // namespace emerick::model
