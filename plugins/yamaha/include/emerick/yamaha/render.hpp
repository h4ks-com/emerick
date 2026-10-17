#pragma once

#include <vector>

#include "emerick/yamaha/chart.hpp"
#include "emerick/yamaha/raw_event.hpp"

namespace emerick::yamaha {

inline constexpr int kChordDetectChannel = 4;
inline constexpr core::Ticks kButtonReleaseTicks = 10;

// Channels count from 1. Each chord sounds until the next one, the last until endTick.
std::vector<RawEvent> chordNotes(const std::vector<ChordEvent>& chords, int channel, core::Ticks endTick);

int sectionCc(const SectionEvent& event);
// Each button press is CC value 127, then 0 ten ticks later.
std::vector<RawEvent> sectionControls(const std::vector<SectionEvent>& sections, int channel);

}  // namespace emerick::yamaha
