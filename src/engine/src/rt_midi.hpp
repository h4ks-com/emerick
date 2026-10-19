#pragma once

#include <cstdint>

#include "emerick/engine/audio_backend.hpp"
#include "rt_process.hpp"
#include "snapshot.hpp"

namespace emerick::engine {

// The block's first frame sits at blockStartFrame on the timeline.
void receiveMidi(Snapshot& snapshot, const RealtimeState& state, const AudioBlock& block, Frames blockStartFrame);
void sendClock(const Snapshot& snapshot, Frames frame, const AudioBlock& block);
void playMidiClips(Snapshot& snapshot, Frames frame, const AudioBlock& block);
void sendToClockPorts(const Snapshot& snapshot, std::uint8_t status, std::int64_t timestampNs);

void releaseNotes(Snapshot& snapshot, std::int64_t timestampNs);
void releaseInaudibleNotes(Snapshot& snapshot, std::int64_t timestampNs);
void sendAllNotesOff(const Snapshot& snapshot, std::int64_t timestampNs);

}  // namespace emerick::engine
