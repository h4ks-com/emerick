#pragma once

#include "emerick/engine/audio_backend.hpp"
#include "rt_process.hpp"
#include "snapshot.hpp"

namespace emerick::engine {

inline constexpr int kMetronomePreviewBars = 2;

// Starts the count-in clicks that fall in the block's first frames, counted from done frames into the count-in.
void playCountIn(RealtimeState& state, const Snapshot& snapshot, const AudioBlock& block, Frames done, Frames frames);
// Starts the clicks or groove hits of the metronome preview that fall in the block's first frames, counted from done
// frames into the preview.
void playMetronomePreview(RealtimeState& state, const Snapshot& snapshot, const AudioBlock& block, Frames done,
                          Frames frames);
// Starts the clicks or groove hits on the timeline that fall in the block from offset on, where the timeline is at
// frame.
void playMetronome(RealtimeState& state, const Snapshot& snapshot, const AudioBlock& block, Frames offset,
                   Frames frame);
// Mixes every playing metronome sound into the metronome outputs.
void renderMetronome(RealtimeState& state, const Snapshot& snapshot, const AudioBlock& block);

}  // namespace emerick::engine
