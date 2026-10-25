#pragma once

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

}  // namespace emerick::yamaha
