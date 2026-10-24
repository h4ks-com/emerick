#pragma once

#include <vector>

#include "emerick/yamaha/chart.hpp"
#include "emerick/yamaha/raw_event.hpp"

namespace emerick::yamaha {

inline constexpr core::Ticks kButtonReleaseTicks = 10;

// One Chord Control sysex per chord, so the keyboard plays its Style on that chord and sounds no note of its own.
std::vector<RawEvent> chordControls(const std::vector<ChordEvent>& chords);

// One Section Control press per section, On then Off ten ticks later, as the keyboard records a button press.
std::vector<RawEvent> sectionControls(const std::vector<SectionEvent>& sections);

}  // namespace emerick::yamaha
