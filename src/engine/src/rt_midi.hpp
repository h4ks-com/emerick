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
// Sends each audible MIDI track's last bank, program, controllers and pitch bend before frame, when the snapshot
// chases.
void chaseMidi(const Snapshot& snapshot, Frames frame, std::int64_t timestampNs);
void sendPanic(Snapshot& snapshot, std::int64_t timestampNs);
// Queues the messages the UI thread sent on the ports the snapshot uses and drops those for any other port.
void sendOutgoingMidi(RealtimeState& state, Snapshot& snapshot, std::int64_t timestampNs);

}  // namespace emerick::engine
