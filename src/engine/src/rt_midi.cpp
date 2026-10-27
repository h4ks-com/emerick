#include "rt_midi.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "clock.hpp"
#include "emerick/core/midi.hpp"

namespace emerick::engine {

namespace {

using core::midi::isChannelMessage;

constexpr Ticks kTicksPerClock = core::kTicksPerQuarter / 24;

std::int64_t timeAt(const AudioBlock& block, double sampleRate, Frames offset) {
    return block.hostTimeNs + framesToNs(offset, sampleRate);
}

// Where a message came from: an input port, or the instrument of the track at an index.
struct MidiFrom {
    const MidiInputPort* port = nullptr;
    std::optional<std::size_t> track;
};

bool listensTo(const TrackState& track, const MidiFrom& from) {
    return from.port != nullptr ? track.midiIn.get() == from.port : from.track && track.midiSource == from.track;
}

bool acceptsMessage(const TrackState& track, const MidiFrom& from, std::span<const std::uint8_t> bytes) {
    if (!listensTo(track, from)) {
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

bool sendsMidi(const TrackState& track) { return track.midiOut || track.instrument; }

// A track's MIDI goes to its output port and to its instrument.
void sendToTrack(const TrackState& track, std::span<const std::uint8_t> bytes, std::int64_t timestampNs) {
    if (track.midiOut) {
        track.midiOut->queue(bytes, timestampNs);
    }
    if (track.instrument) {
        track.instrument->queueMidi(bytes, timestampNs);
    }
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
    if (!sendsMidi(track)) {
        return;
    }
    for (std::size_t channel = 0; channel < kMidiChannels; ++channel) {
        std::bitset<kMidiNotes>& notes = track.soundingNotes[channel];
        for (std::size_t note = 0; notes.any() && note < kMidiNotes; ++note) {
            if (notes.test(note)) {
                const std::array<std::uint8_t, 3> off{static_cast<std::uint8_t>(core::midi::kNoteOff | channel),
                                                      static_cast<std::uint8_t>(note), 0};
                sendToTrack(track, off, timestampNs);
                notes.reset(note);
            }
        }
    }
}

// A sysex from a port goes to the tracks that claim it, or to the first recording track; a sysex from an instrument
// goes to every track that takes all its channels.
void route(Snapshot& snapshot, const RealtimeState& state, const MidiFrom& from, std::span<const std::uint8_t> bytes,
           std::int64_t timestampNs, Frames eventFrame) {
    const bool claimedSysex = bytes[0] == core::midi::kSysexStart && from.port != nullptr;
    const bool soloActive = anySolo(snapshot);
    for (TrackState& track : snapshot.tracks) {
        const bool accepts =
            claimedSysex ? listensTo(track, from) && track.claimsSysex : acceptsMessage(track, from, bytes);
        if (track.thru && track.midiOut && accepts) {
            std::array<std::uint8_t, 3> rewritten{};
            track.midiOut->queue(onChannel(bytes, track.outChannel, rewritten), timestampNs);
        }
        // An instrument plays what the player plays while its track is armed or monitored, as Logic's does, and
        // always plays what another instrument sends it, as it plays a clip.
        const bool plays = track.armed || track.monitor || from.track.has_value();
        if (track.instrument && accepts && plays && audible(track, soloActive)) {
            track.instrument->queueMidi(bytes, timestampNs);
            rememberNote(track, bytes);
        }
    }
    if (!state.taking) {
        return;
    }
    const auto record = [&](TrackState& track) {
        const Frames frame = std::max(eventFrame + track.recordDelay, state.takeStartFrame);
        track.recorder->writeMidi(snapshot.tempo.framesToTicks(frame, snapshot.sampleRate), bytes);
    };
    const auto recording = [&](const TrackState& track) {
        return track.kind == TrackKind::midi && listensTo(track, from) && track.recorder && track.recorder->recording();
    };
    if (!claimedSysex) {
        for (TrackState& track : snapshot.tracks) {
            if (recording(track) && acceptsMessage(track, from, bytes)) {
                record(track);
            }
        }
        return;
    }
    bool claimed = false;
    for (TrackState& track : snapshot.tracks) {
        if (recording(track) && track.claimsSysex) {
            record(track);
            claimed = true;
        }
    }
    if (!claimed) {
        const auto first = std::ranges::find_if(snapshot.tracks, recording);
        if (first != snapshot.tracks.end()) {
            record(*first);
        }
    }
}

void countLostMidi(Snapshot& snapshot, const RealtimeState& state, MidiInputPort* port) {
    const std::uint32_t lost = port->lost.exchange(0, std::memory_order_relaxed);
    if (lost == 0 || !state.taking) {
        return;
    }
    for (TrackState& track : snapshot.tracks) {
        if (track.midiIn.get() == port && track.recorder && track.recorder->recording()) {
            track.recorder->countLostMidi(lost);
        }
    }
}

// What a chase found on one channel; kUnset marks a value no event set.
struct ChasedChannel {
    static constexpr std::uint8_t kUnset = 0xFF;
    static constexpr std::uint16_t kUnsetBend = 0xFFFF;
    std::array<std::uint8_t, kMidiNotes> controllers{};
    std::uint8_t program = kUnset;
    std::uint16_t bend = kUnsetBend;

    ChasedChannel() { controllers.fill(kUnset); }
};

void chaseTrack(const TrackState& track, Frames frame, std::int64_t timestampNs) {
    std::array<ChasedChannel, kMidiChannels> chased{};
    for (const ScheduledMidi& event : track.midi->chaseEvents) {
        if (event.frame >= frame) {
            break;
        }
        std::array<std::uint8_t, 3> rewritten{};
        const auto bytes =
            onChannel(std::span(track.midi->bytes).subspan(event.offset, event.size), track.outChannel, rewritten);
        ChasedChannel& channel = chased[bytes[0] & 0x0F];
        switch (bytes[0] & 0xF0) {
            case core::midi::kControlChange:
                channel.controllers[bytes[1] & 0x7F] = bytes[2] & 0x7F;
                break;
            case core::midi::kProgramChange:
                channel.program = bytes[1] & 0x7F;
                break;
            default:
                channel.bend = static_cast<std::uint16_t>((bytes[1] & 0x7F) | ((bytes[2] & 0x7F) << 7));
                break;
        }
    }
    const auto send = [&](std::span<const std::uint8_t> bytes) { sendToTrack(track, bytes, timestampNs); };
    for (std::size_t index = 0; index < kMidiChannels; ++index) {
        const ChasedChannel& channel = chased[index];
        const auto status = [&](std::uint8_t type) { return static_cast<std::uint8_t>(type | index); };
        const auto sendController = [&](std::size_t controller) {
            if (channel.controllers[controller] != ChasedChannel::kUnset) {
                send(std::array{status(core::midi::kControlChange), static_cast<std::uint8_t>(controller),
                                channel.controllers[controller]});
            }
        };
        sendController(core::midi::kBankSelectCc);
        sendController(core::midi::kBankSelectLsbCc);
        if (channel.program != ChasedChannel::kUnset) {
            send(std::array{status(core::midi::kProgramChange), channel.program});
        }
        for (std::size_t controller = 0; controller < kMidiNotes; ++controller) {
            if (controller != core::midi::kBankSelectCc && controller != core::midi::kBankSelectLsbCc) {
                sendController(controller);
            }
        }
        if (channel.bend != ChasedChannel::kUnsetBend) {
            send(std::array{status(core::midi::kPitchBend), static_cast<std::uint8_t>(channel.bend & 0x7F),
                            static_cast<std::uint8_t>(channel.bend >> 7)});
        }
    }
}

}  // namespace

void receiveMidi(Snapshot& snapshot, const RealtimeState& state, const AudioBlock& block, Frames blockStartFrame) {
    for (const auto& port : snapshot.inputs) {
        countLostMidi(snapshot, state, port.get());
        MidiRecordHeader header;
        while (readValue(port->ring, header)) {
            if (header.size == 0 || header.size > snapshot.incoming.size()) {
                port->ring.skip(header.size);
                continue;
            }
            const auto bytes = std::span(snapshot.incoming).first(header.size);
            readInto(port->ring, bytes);
            const Frames offset = nsToFrames(header.timestampNs - block.hostTimeNs, snapshot.sampleRate);
            const Frames eventFrame = blockStartFrame + std::clamp<Frames>(offset, -block.frames, block.frames - 1);
            route(snapshot, state, {.port = port.get(), .track = std::nullopt}, bytes, header.timestampNs, eventFrame);
        }
    }
}

void routeInstrumentMidi(Snapshot& snapshot, const RealtimeState& state, std::size_t source, const AudioBlock& block,
                         Frames blockStartFrame) {
    for (const InstrumentMidi& message : snapshot.tracks[source].instrument->producedMidi()) {
        route(snapshot, state, {.port = nullptr, .track = source}, message.bytes,
              timeAt(block, snapshot.sampleRate, message.offset), blockStartFrame + message.offset);
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
        if (!sendsMidi(track) || !audible(track, soloActive)) {
            continue;
        }
        const auto first = std::ranges::lower_bound(track.midi->events, frame, {}, &ScheduledMidi::frame);
        for (auto event = first; event != track.midi->events.end() && event->frame < frame + block.frames; ++event) {
            std::array<std::uint8_t, 3> rewritten{};
            const auto sent = onChannel(std::span(track.midi->bytes).subspan(event->offset, event->size),
                                        track.outChannel, rewritten);
            sendToTrack(track, sent, timeAt(block, snapshot.sampleRate, event->frame - frame));
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
        if (track.kind != TrackKind::midi || !sendsMidi(track)) {
            continue;
        }
        const int first = track.outChannel == 0 ? 1 : track.outChannel;
        const int last = track.outChannel == 0 ? 16 : track.outChannel;
        for (int channel = first; channel <= last; ++channel) {
            const std::array<std::uint8_t, 3> message{core::midi::statusFor(core::midi::kControlChange, channel),
                                                      core::midi::kAllNotesOffCc, 0};
            sendToTrack(track, message, timestampNs);
        }
    }
}

void chaseMidi(const Snapshot& snapshot, Frames frame, std::int64_t timestampNs) {
    if (!snapshot.chaseMidi) {
        return;
    }
    const bool soloActive = anySolo(snapshot);
    for (const TrackState& track : snapshot.tracks) {
        if (sendsMidi(track) && !track.midi->chaseEvents.empty() && audible(track, soloActive)) {
            chaseTrack(track, frame, timestampNs);
        }
    }
}

void sendPanic(Snapshot& snapshot, std::int64_t timestampNs) {
    sendToClockPorts(snapshot, core::midi::kStop, timestampNs);
    releaseNotes(snapshot, timestampNs);
    const auto silence = [&](const auto& send) {
        for (std::size_t channel = 0; channel < kMidiChannels; ++channel) {
            const auto status = static_cast<std::uint8_t>(core::midi::kControlChange | channel);
            for (const std::uint8_t controller :
                 {core::midi::kSustainCc, core::midi::kAllSoundOffCc, core::midi::kAllNotesOffCc}) {
                send(std::array<std::uint8_t, 3>{status, controller, 0});
            }
        }
    };
    for (const auto& port : snapshot.outputs) {
        silence([&](std::span<const std::uint8_t> bytes) { port->queue(bytes, timestampNs); });
    }
    for (const TrackState& track : snapshot.tracks) {
        if (track.instrument) {
            silence([&](std::span<const std::uint8_t> bytes) { track.instrument->queueMidi(bytes, timestampNs); });
        }
    }
}

void sendOutgoingMidi(RealtimeState& state, Snapshot& snapshot, std::int64_t timestampNs) {
    const auto named = [](std::span<const std::uint8_t> id, std::string_view name) {
        return std::ranges::equal(id, name, {}, {}, [](char letter) { return static_cast<std::uint8_t>(letter); });
    };
    OutgoingMidiHeader header;
    while (readValue(state.outgoingMidi, header)) {
        const std::size_t size = std::size_t{header.targetIdSize} + header.size;
        if (size > snapshot.incoming.size()) {
            state.outgoingMidi.skip(size);
            continue;
        }
        const auto record = std::span(snapshot.incoming).first(size);
        readInto(state.outgoingMidi, record);
        const auto targetId = record.first(header.targetIdSize);
        const auto bytes = record.subspan(header.targetIdSize);
        if (header.toInstrument) {
            const auto track = std::ranges::find_if(
                snapshot.tracks, [&](const TrackState& candidate) { return named(targetId, candidate.id); });
            if (track != snapshot.tracks.end() && track->instrument && audible(*track, anySolo(snapshot))) {
                track->instrument->queueMidi(bytes, timestampNs);
                rememberNote(*track, bytes);
            }
            continue;
        }
        const auto port =
            std::ranges::find_if(snapshot.outputs, [&](const auto& output) { return named(targetId, output->id()); });
        if (port != snapshot.outputs.end()) {
            (*port)->queue(bytes, timestampNs);
        }
    }
}

}  // namespace emerick::engine
