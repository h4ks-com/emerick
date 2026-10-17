#include "rt_midi.hpp"

#include <algorithm>
#include <array>
#include <span>

#include "clock.hpp"
#include "emerick/core/midi.hpp"

namespace emerick::engine {

namespace {

using core::midi::isChannelMessage;

constexpr Ticks kTicksPerClock = core::kTicksPerQuarter / 24;

std::int64_t timeAt(const AudioBlock& block, double sampleRate, Frames offset) {
    return block.hostTimeNs + framesToNs(offset, sampleRate);
}

bool acceptsMessage(const TrackState& track, const MidiInputPort* port, std::span<const std::uint8_t> bytes) {
    if (track.midiIn.get() != port) {
        return false;
    }
    if (!isChannelMessage(bytes[0])) {
        return track.inChannel == 0;
    }
    return track.inChannel == 0 || track.inChannel == (bytes[0] & 0x0F) + 1;
}

std::span<const std::uint8_t> onChannel(std::span<const std::uint8_t> bytes, int channel,
                                        std::array<std::uint8_t, 3>& rewritten) {
    if (channel == 0 || !isChannelMessage(bytes[0]) || bytes.size() > rewritten.size()) {
        return bytes;
    }
    std::ranges::copy(bytes, rewritten.begin());
    rewritten[0] = core::midi::statusFor(bytes[0] & 0xF0, channel);
    return std::span(rewritten).first(bytes.size());
}

void rememberNote(TrackState& track, std::span<const std::uint8_t> sent) {
    if (sent.size() != 3 || !isChannelMessage(sent[0])) {
        return;
    }
    const auto type = static_cast<std::uint8_t>(sent[0] & 0xF0);
    std::bitset<kMidiNotes>& notes = track.soundingNotes[sent[0] & 0x0F];
    const std::size_t note = sent[1] & 0x7F;
    if (type == core::midi::kNoteOn && sent[2] > 0) {
        notes.set(note);
    } else if (type == core::midi::kNoteOff || type == core::midi::kNoteOn) {
        notes.reset(note);
    }
}

void releaseTrackNotes(TrackState& track, std::int64_t timestampNs) {
    if (!track.midiOut) {
        return;
    }
    for (std::size_t channel = 0; channel < kMidiChannels; ++channel) {
        std::bitset<kMidiNotes>& notes = track.soundingNotes[channel];
        for (std::size_t note = 0; notes.any() && note < kMidiNotes; ++note) {
            if (notes.test(note)) {
                const std::array<std::uint8_t, 3> off{static_cast<std::uint8_t>(core::midi::kNoteOff | channel),
                                                      static_cast<std::uint8_t>(note), 0};
                track.midiOut->queue(off, timestampNs);
                notes.reset(note);
            }
        }
    }
}

void route(Snapshot& snapshot, const RealtimeState& state, const MidiInputPort* port,
           std::span<const std::uint8_t> bytes, std::int64_t timestampNs, Frames eventFrame) {
    const bool sysex = bytes[0] == core::midi::kSysexStart;
    for (const TrackState& track : snapshot.tracks) {
        const bool accepts =
            sysex ? track.midiIn.get() == port && track.claimsSysex : acceptsMessage(track, port, bytes);
        if (track.thru && track.midiOut && accepts) {
            std::array<std::uint8_t, 3> rewritten{};
            track.midiOut->queue(onChannel(bytes, track.outChannel, rewritten), timestampNs);
        }
    }
    if (!state.taking) {
        return;
    }
    const Ticks tick = snapshot.tempo.framesToTicks(std::max(eventFrame, state.takeStartFrame), snapshot.sampleRate);
    const auto recording = [&](const TrackState& track) {
        return track.kind == TrackKind::midi && track.midiIn.get() == port && track.recorder &&
               track.recorder->recording();
    };
    if (!sysex) {
        for (TrackState& track : snapshot.tracks) {
            if (recording(track) && acceptsMessage(track, port, bytes)) {
                track.recorder->writeMidi(tick, bytes);
            }
        }
        return;
    }
    bool claimed = false;
    for (TrackState& track : snapshot.tracks) {
        if (recording(track) && track.claimsSysex) {
            track.recorder->writeMidi(tick, bytes);
            claimed = true;
        }
    }
    if (!claimed) {
        const auto first = std::ranges::find_if(snapshot.tracks, recording);
        if (first != snapshot.tracks.end()) {
            first->recorder->writeMidi(tick, bytes);
        }
    }
}

}  // namespace

void receiveMidi(Snapshot& snapshot, const RealtimeState& state, const AudioBlock& block) {
    for (const auto& port : snapshot.inputs) {
        MidiRecordHeader header;
        while (readValue(port->ring, header)) {
            if (header.size == 0 || header.size > snapshot.incoming.size()) {
                port->ring.skip(header.size);
                continue;
            }
            const auto bytes = std::span(snapshot.incoming).first(header.size);
            readInto(port->ring, bytes);
            const Frames offset = nsToFrames(header.timestampNs - block.hostTimeNs, snapshot.sampleRate);
            const Frames eventFrame = state.frame + std::clamp<Frames>(offset, -block.frames, block.frames - 1);
            route(snapshot, state, port.get(), bytes, header.timestampNs, eventFrame);
        }
    }
}

void sendClock(const Snapshot& snapshot, Frames frame, const AudioBlock& block) {
    if (snapshot.clockPorts.empty()) {
        return;
    }
    const double rate = snapshot.sampleRate;
    Ticks pulse = std::max<Ticks>(0, snapshot.tempo.framesToTicks(frame, rate) / kTicksPerClock - 1);
    for (;; ++pulse) {
        const Frames at = snapshot.tempo.ticksToFrames(pulse * kTicksPerClock, rate);
        if (at < frame) {
            continue;
        }
        if (at >= frame + block.frames) {
            break;
        }
        sendToClockPorts(snapshot, core::midi::kClock, timeAt(block, rate, at - frame));
    }
}

void playMidiClips(Snapshot& snapshot, Frames frame, const AudioBlock& block) {
    const bool soloActive = anySolo(snapshot);
    for (TrackState& track : snapshot.tracks) {
        if (!track.midiOut || !audible(track, soloActive)) {
            continue;
        }
        const auto first = std::ranges::lower_bound(track.midiEvents, frame, {}, &ScheduledMidi::frame);
        for (auto event = first; event != track.midiEvents.end() && event->frame < frame + block.frames; ++event) {
            std::array<std::uint8_t, 3> rewritten{};
            const auto sent =
                onChannel(std::span(track.midiBytes).subspan(event->offset, event->size), track.outChannel, rewritten);
            track.midiOut->queue(sent, timeAt(block, snapshot.sampleRate, event->frame - frame));
            rememberNote(track, sent);
        }
    }
}

void sendToClockPorts(const Snapshot& snapshot, std::uint8_t status, std::int64_t timestampNs) {
    for (const auto& port : snapshot.clockPorts) {
        port->queue(std::span(&status, 1), timestampNs);
    }
}

void releaseNotes(Snapshot& snapshot, std::int64_t timestampNs) {
    for (TrackState& track : snapshot.tracks) {
        releaseTrackNotes(track, timestampNs);
    }
}

// Inaudible tracks only hold notes from before a mute or solo cut them off, so we release all of them.
void releaseInaudibleNotes(Snapshot& snapshot, std::int64_t timestampNs) {
    const bool soloActive = anySolo(snapshot);
    for (TrackState& track : snapshot.tracks) {
        if (!audible(track, soloActive)) {
            releaseTrackNotes(track, timestampNs);
        }
    }
}

void sendAllNotesOff(const Snapshot& snapshot, std::int64_t timestampNs) {
    for (const TrackState& track : snapshot.tracks) {
        if (track.kind != TrackKind::midi || !track.midiOut) {
            continue;
        }
        const int first = track.outChannel == 0 ? 1 : track.outChannel;
        const int last = track.outChannel == 0 ? 16 : track.outChannel;
        for (int channel = first; channel <= last; ++channel) {
            const std::array<std::uint8_t, 3> message{core::midi::statusFor(core::midi::kControlChange, channel),
                                                      core::midi::kAllNotesOffCc, 0};
            track.midiOut->queue(message, timestampNs);
        }
    }
}

}  // namespace emerick::engine
