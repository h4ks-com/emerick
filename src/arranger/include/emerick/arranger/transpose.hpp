#pragma once

#include <optional>

#include "emerick/arranger/style.hpp"
#include "emerick/yamaha/chord.hpp"

namespace emerick::arranger {

// A MegaVoice part keeps its notes from 84 up, which play strums and noises, where the style wrote them.
inline constexpr int kMegaVoiceFxNote = 84;
inline constexpr int kMegaVoiceBankMsb = 8;

bool playsOn(const ChannelRule& rule, const yamaha::Chord& chord);

// The transposition a source note follows: SFF2 splits the source notes into three ranges.
const Transposition& transpositionFor(const ChannelRule& rule, int sourceNote);

int transposeNote(const ChannelRule& rule, int sourceNote, const yamaha::Chord& chord, bool megaVoice);

// The note a held note moves to when the chord changes under it, by its retrigger rule, or none when it stops.
std::optional<int> retriggerNote(const ChannelRule& rule, int sourceNote, int heldNote, const yamaha::Chord& chord,
                                 bool megaVoice);

}  // namespace emerick::arranger
