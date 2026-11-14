#pragma once

#include <random>
#include <vector>

#include "emerick/model/document.hpp"

namespace emerick::model {

inline constexpr int kMaxPitch = 127;
inline constexpr int kMaxVelocity = 127;

// Moves each note-on and its note-off from the clip's events into its notes; a note still held at the clip end lasts
// until the end.
void gatherNotes(MidiClip& clip);

bool looped(const MidiClip& clip);
// How long the clip runs on the timeline, its repeats included.
Ticks clipSpan(const MidiClip& clip);
// The clip with each repeat written out as notes and events of its own, so it no longer loops.
MidiClip unrolled(const MidiClip& clip);

// The notes as note-ons and note-offs merged in time order with the other events inside the clip and its repeats, as
// playback and export send them. Every note ends by the end of its repeat, and a note-off goes before a note-on at the
// same tick.
std::vector<MidiEvent> clipEvents(const MidiClip& clip);

inline constexpr double kStraightSwing = 50.0;
inline constexpr double kMaxSwing = 100.0;

// Moves at toward its nearest grid step by strength, from 0 (unchanged) to 1 (on the grid). Swing from 0 to 100 places
// every odd step at swing / 50 grid steps after the step before it: 50 is straight, 66.7 a triplet feel and 75 dotted.
Ticks quantized(Ticks at, Ticks grid, double strength, double swing = kStraightSwing);

// The largest random change humanize makes to a note.
struct Humanize {
    Ticks timing = 0;
    // The chance from 0 to 1 that a note's start stays where it is.
    double keep = 0.0;
    int velocity = 0;
    // A share of the note's length.
    double length = 0.0;
};

// Moves the note's start, velocity and length by random amounts up to each spread, small ones more often than large,
// keeping its start inside the clip, its end no further past the clip end than before and its velocity from 1 to 127.
void humanize(Note& note, Ticks clipLength, const Humanize& amounts, std::mt19937& random);

}  // namespace emerick::model
