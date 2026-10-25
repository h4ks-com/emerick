#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "emerick/yamaha/chart.hpp"
#include "emerick/yamaha/raw_event.hpp"

namespace emerick::yamaha {

inline constexpr core::Ticks kButtonReleaseTicks = 10;
inline constexpr std::uint8_t kButtonDown = 127;

// One Chord Control sysex per chord, so the keyboard plays its Style on that chord and sounds no note of its own.
std::vector<RawEvent> chordControls(const std::vector<ChordEvent>& chords);

// One External Controller button press per section on channel (1 to 16), value 127 then 0 ten ticks later. The
// keyboard starts a fill or a break at once and switches a Main, Intro or Ending at the next bar line, so we press
// those leadTicks early. A Main the fill before it already leads to needs no press.
std::vector<RawEvent> sectionPresses(const std::vector<SectionEvent>& sections, int channel, core::Ticks leadTicks);

std::vector<std::vector<std::uint8_t>> buttonPress(std::uint8_t cc, int channel);
std::vector<std::vector<std::uint8_t>> sectionPress(const SectionEvent& section, int channel);

// The section the keyboard has to be on to play position: a fill leads to its Main, and a Break returns to the Main
// it interrupts.
std::optional<SectionEvent> chaseSection(const std::vector<SectionEvent>& sections, core::Ticks position);

// The last Main, Intro or Ending at or before a position, the bar line its current pass began on, the bar of that
// pass counted from 1, and how many bars a pass of a Main takes (0 for an Intro or an Ending, which play once).
struct PatternPosition {
    SectionEvent section;
    core::Ticks passStart = 0;
    int bar = 1;
    int bars = 0;
};
std::optional<PatternPosition> patternAt(const std::vector<SectionEvent>& sections, core::Ticks position,
                                         core::Ticks ticksPerBar);

}  // namespace emerick::yamaha
