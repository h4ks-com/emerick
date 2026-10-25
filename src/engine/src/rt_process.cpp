// Everything here runs on the audio thread, so it works only on preallocated memory and lock-free queues.

#include "rt_process.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <numbers>
#include <span>
#include <utility>

#include "clock.hpp"
#include "emerick/core/midi.hpp"
#include "emerick/engine/engine.hpp"
#include "rt_metronome.hpp"
#include "rt_midi.hpp"

namespace emerick::engine {

namespace {

// The timeline frame at an offset into the block, which moves only while the transport runs, from where a count-in
// ended.
// A cycle jump at jumpOffset puts the timeline back at jumpFrame from there on.
struct BlockTime {
    Frames frame = 0;
    Frames rollFrom = 0;
    bool playing = false;
    Frames jumpOffset = 0;
    Frames jumpFrame = -1;

    [[nodiscard]] Frames at(Frames offset) const {
        if (!playing) {
            return frame;
        }
        return jumpFrame >= 0 && offset >= jumpOffset ? jumpFrame + offset - jumpOffset
                                                      : frame + std::max<Frames>(offset - rollFrom, 0);
    }
};

// A rebuilt snapshot starts each track from the gains the old one played, so an edit ramps to its new gain.
void keepOutputGains(const Snapshot& from, Snapshot& to) {
    for (TrackState& track : to.tracks) {
        const auto old = std::ranges::find(from.tracks, track.id, &TrackState::id);
        if (old != from.tracks.end() && old->outputGains.size() == track.outputGains.size()) {
            std::ranges::copy(old->outputGains, track.outputGains.begin());
        }
        if (old != from.tracks.end() && old->sends.size() == track.sends.size()) {
            for (std::size_t index = 0; index < track.sends.size(); ++index) {
                track.sends[index].rampFrom = old->sends[index].rampFrom;
            }
        }
    }
}

void adoptPending(RealtimeState& state) {
    if (state.pending.load(std::memory_order_acquire) == nullptr) {
        return;
    }
    if (state.current != nullptr && state.retired.size_approx() >= state.retired.max_capacity()) {
        return;
    }
    Snapshot* next = state.pending.exchange(nullptr, std::memory_order_acq_rel);
    if (state.current != nullptr) {
        keepOutputGains(*state.current, *next);
        state.retired.try_enqueue(state.current);
    }
    state.current = next;
}

TrackState* findTrack(Snapshot& snapshot, std::string_view id) {
    const auto found = std::ranges::find(snapshot.tracks, id, &TrackState::id);
    return found == snapshot.tracks.end() ? nullptr : &*found;
}

void endTake(RealtimeState& state, Snapshot& snapshot) {
    if (!state.taking) {
        return;
    }
    state.taking = false;
    for (TrackState& track : snapshot.tracks) {
        if (track.recorder && track.recorder->recording()) {
            track.recorder->end();
        }
    }
}

void updateTake(RealtimeState& state, Snapshot& snapshot) {
    const bool shouldTake = state.recordEnabled && state.transportPlaying;
    if (shouldTake && !state.taking) {
        state.taking = true;
        state.takeStartFrame = state.frame;
        const Ticks startTick = snapshot.tempo.framesToTicks(state.frame, snapshot.sampleRate);
        for (TrackState& track : snapshot.tracks) {
            if (track.armed && track.recorder) {
                track.recorder->begin(startTick, track.recordDelay);
            }
        }
    } else if (!shouldTake) {
        endTake(state, snapshot);
    }
}

void locate(RealtimeState& state, Snapshot& snapshot, Ticks ticks, std::int64_t timestampNs) {
    endTake(state, snapshot);
    releaseNotes(snapshot, timestampNs);
    state.frame = snapshot.tempo.ticksToFrames(std::max<Ticks>(ticks, 0), snapshot.sampleRate);
    for (TrackState& track : snapshot.tracks) {
        for (ClipPlayback& clip : track.clips) {
            clip.stream->seek(std::clamp<Frames>(state.frame - clip.start, 0, clip.length));
        }
    }
    if (state.transportPlaying) {
        chaseMidi(snapshot, state.frame, timestampNs);
    }
    updateTake(state, snapshot);
    state.locatesApplied.fetch_add(1, std::memory_order_release);
}

void startTransport(RealtimeState& state, Snapshot& snapshot, std::int64_t timestampNs) {
    state.transportPlaying = true;
    state.startPending = true;
    chaseMidi(snapshot, state.frame, timestampNs);
    updateTake(state, snapshot);
}

void sendPendingStart(RealtimeState& state, const Snapshot& snapshot, std::int64_t timestampNs) {
    if (std::exchange(state.startPending, false)) {
        sendToClockPorts(snapshot, core::midi::kStart, timestampNs);
    }
}

void apply(RealtimeState& state, Snapshot& snapshot, const Command& command, const AudioBlock& block) {
    TrackState* track = findTrack(snapshot, command.track());
    switch (command.type) {
        case Command::Type::play:
            if (state.transportPlaying || state.countInLeft > 0) {
                break;
            }
            if (state.recordEnabled && snapshot.metronome.countInFrames > 0) {
                state.countInLeft = snapshot.metronome.countInFrames;
            } else {
                startTransport(state, snapshot, block.hostTimeNs);
            }
            break;
        case Command::Type::stop:
            state.countInLeft = 0;
            if (state.transportPlaying) {
                state.transportPlaying = false;
                state.startPending = false;
                sendToClockPorts(snapshot, core::midi::kStop, block.hostTimeNs);
                releaseNotes(snapshot, block.hostTimeNs);
                sendAllNotesOff(snapshot, block.hostTimeNs);
                updateTake(state, snapshot);
            }
            break;
        case Command::Type::locate:
            locate(state, snapshot, command.ticks, block.hostTimeNs);
            break;
        case Command::Type::record:
            state.recordEnabled = command.on;
            updateTake(state, snapshot);
            break;
        case Command::Type::mute:
            if (track != nullptr) {
                track->mute = command.on;
                releaseInaudibleNotes(snapshot, block.hostTimeNs);
            }
            break;
        case Command::Type::solo:
            if (track != nullptr) {
                track->solo = command.on;
                updateSoloPaths(snapshot);
                releaseInaudibleNotes(snapshot, block.hostTimeNs);
            }
            break;
        case Command::Type::arm:
            if (track != nullptr) {
                track->armed = command.on;
            }
            break;
        case Command::Type::gain:
            if (track != nullptr) {
                track->gain = command.gain;
            }
            break;
        case Command::Type::pan:
            if (track != nullptr) {
                track->pan = command.pan;
            }
            break;
        case Command::Type::panic:
            sendPanic(snapshot, block.hostTimeNs);
            break;
    }
}

Frames solidStart(const ClipPlayback& clip) { return clip.start + clip.fadeIn; }

Frames solidEnd(const ClipPlayback& clip) { return clip.start + clip.length - clip.fadeOut; }

// A later clip silences earlier ones wherever it plays at full gain, so the newest take wins and fades crossfade.
// ponytail: scans every later clip per frame of an overlap, an interval list if tracks stack hundreds of takes.
bool covered(std::span<const ClipPlayback> later, Frames at) {
    return std::ranges::any_of(
        later, [at](const ClipPlayback& clip) { return at >= solidStart(clip) && at < solidEnd(clip); });
}

bool anyCovers(std::span<const ClipPlayback> later, Frames from, Frames to) {
    return std::ranges::any_of(
        later, [&](const ClipPlayback& clip) { return solidStart(clip) < to && solidEnd(clip) > from; });
}

// Plays frames from frame on into each track's mix, starting offset frames into the block.
void playAudioClips(Snapshot& snapshot, Frames frame, Frames offset, Frames frames) {
    const bool soloActive = anySolo(snapshot);
    const Frames blockEnd = frame + frames;
    for (TrackState& track : snapshot.tracks) {
        if (track.mix.empty() || !audible(track, soloActive)) {
            continue;
        }
        const std::span<const ClipPlayback> clips = track.clips;
        for (std::size_t index = 0; index < clips.size(); ++index) {
            const ClipPlayback& clip = clips[index];
            const Frames from = std::max(frame, clip.start);
            const Frames to = std::min(blockEnd, clip.start + clip.length);
            if (from >= to) {
                continue;
            }
            const std::span<const ClipPlayback> later = clips.subspan(index + 1);
            const bool overlapped = anyCovers(later, from, to);
            const Frames local = from - clip.start;
            const Frames count = to - from;
            const std::span<const float> samples = clip.stream->read(local, count, clip.preload.get());
            const auto channels = static_cast<std::size_t>(clip.stream->channels());
            const auto first = static_cast<std::size_t>(offset + (from - frame));
            for (Frames at = 0; at < count; ++at) {
                if (overlapped && covered(later, from + at)) {
                    continue;
                }
                const float gain = clip.gain * fadeGain(local + at, clip.length, clip.fadeIn, clip.fadeOut,
                                                        clip.fadeInCurve, clip.fadeOutCurve);
                const auto frameIndex = static_cast<std::size_t>(at);
                for (std::size_t channel = 0; channel < track.mix.size(); ++channel) {
                    track.mix[channel][first + frameIndex] +=
                        samples[frameIndex * channels + std::min(channel, channels - 1)] * gain;
                }
            }
        }
    }
}

float peakOf(const float* samples, Frames frames) {
    float peak = 0.0F;
    for (Frames frame = 0; frame < frames; ++frame) {
        peak = std::max(peak, std::abs(samples[frame]));
    }
    return peak;
}

void raise(LevelMeter& meter, std::size_t channel, float peak) {
    std::atomic<float>& held = meter.peaks.at(std::min(channel, kMeterChannels - 1));
    if (peak > held.load(std::memory_order_relaxed)) {
        held.store(peak, std::memory_order_relaxed);
    }
    if (peak >= kFullScale) {
        meter.clipped.store(true, std::memory_order_relaxed);
    }
}

void rampGain(float* samples, Frames frames, float& from, float to) {
    const float step = (to - from) / static_cast<float>(frames);
    for (Frames index = 0; index < frames; ++index) {
        samples[index] *= from + (step * static_cast<float>(index + 1));
    }
    from = to;
}

void addRamped(float* destination, const float* source, Frames frames, float from, float to) {
    const float step = (to - from) / static_cast<float>(frames);
    for (Frames index = 0; index < frames; ++index) {
        destination[index] += source[index] * (from + (step * static_cast<float>(index + 1)));
    }
}

// A track with fewer channels than the bus feeds its last channel to the rest, so a mono track reaches both sides.
void feedBus(TrackState& bus, const TrackState& track, Frames offset, Frames frames, float from, float to) {
    const auto first = static_cast<std::size_t>(offset);
    for (std::size_t channel = 0; channel < bus.mix.size(); ++channel) {
        addRamped(bus.mix[channel].data() + first, track.mix[std::min(channel, track.mix.size() - 1)].data() + first,
                  frames, from, to);
    }
}

void playSends(Snapshot& snapshot, TrackState& track, bool pre, Frames offset, Frames frames) {
    for (SendState& send : track.sends) {
        if (send.pre == pre) {
            const float gain = send.gain * track.muteGain;
            feedBus(snapshot.tracks[send.bus], track, offset, frames, send.rampFrom, gain);
            send.rampFrom = gain;
        }
    }
}

float laneValue(const LaneState& lane, Frames frame) {
    const auto& points = lane.points;
    const auto next = std::ranges::upper_bound(points, frame, {}, &LanePoint::frame);
    if (next == points.begin()) {
        return points.front().value;
    }
    if (next == points.end()) {
        return points.back().value;
    }
    const LanePoint& from = *std::prev(next);
    const double t = static_cast<double>(frame - from.frame) / static_cast<double>(next->frame - from.frame);
    return static_cast<float>(core::automationBetween(from.value, next->value, t, from.curve));
}

float gainOfDb(float db) { return std::pow(10.0F, db / 20.0F); }

void applyLanes(TrackState& track, Frames frame) {
    for (const LaneState& lane : track.lanes) {
        const float value = laneValue(lane, frame);
        switch (lane.target) {
            case AutomationTarget::gain:
                track.gain = gainOfDb(value);
                break;
            case AutomationTarget::pan:
                track.pan = value;
                break;
            case AutomationTarget::mute:
                track.muteGain = value >= 0.5F ? 0.0F : 1.0F;
                break;
            case AutomationTarget::send:
                track.sends[lane.send].gain = gainOfDb(value);
                break;
            case AutomationTarget::insert:
                track.inserts[lane.insert].params.at(lane.param) = value;
                break;
        }
    }
}

void mixPart(Snapshot& snapshot, TrackState& track, Frames offset, Frames frames) {
    playSends(snapshot, track, true, offset, frames);
    for (std::size_t channel = 0; channel < track.mix.size(); ++channel) {
        rampGain(track.mix[channel].data() + offset, frames, track.outputGains[channel],
                 outputGain(track, channel, snapshot.panLaw));
    }
    playSends(snapshot, track, false, offset, frames);
}

// An automated track mixes in parts this long, each ramping to the lanes' values at its end.
constexpr Frames kAutomationStep = 32;

// In mix order, so each bus has every source in its mix before its turn: runs each track's inserts, applies its
// fader and pan, feeds
// its sends, meters it, adds it to its outputs or its output bus and clears it for the next block.
void mixTracks(Snapshot& snapshot, const AudioBlock& block, const BlockTime& time) {
    if (block.frames == 0) {
        return;
    }
    const bool soloActive = anySolo(snapshot);
    for (const std::size_t index : snapshot.mixOrder) {
        TrackState& track = snapshot.tracks[index];
        if (!track.mix.empty() && audible(track, soloActive)) {
            if (!track.lanes.empty()) {
                applyLanes(track, time.at(block.frames));
            }
            for (InsertState& insert : track.inserts) {
                insert.effect->process(track.mixPointers, block.frames, insert.params, insert.bypass,
                                       snapshot.tempo.bpm);
            }
            const Frames step = track.lanes.empty() ? block.frames : kAutomationStep;
            for (Frames offset = 0; offset < block.frames; offset += step) {
                const Frames frames = std::min(step, block.frames - offset);
                applyLanes(track, time.at(offset + frames));
                mixPart(snapshot, track, offset, frames);
            }
            for (std::size_t channel = 0; channel < track.mix.size(); ++channel) {
                const float* mixed = track.mix[channel].data();
                raise(*track.outputMeter, channel, peakOf(mixed, block.frames));
                if (channel < track.outputs.size()) {
                    float* destination = block.outputs[static_cast<std::size_t>(track.outputs[channel])];
                    std::transform(mixed, mixed + block.frames, destination, destination, std::plus{});
                }
            }
            if (track.outputBus) {
                feedBus(snapshot.tracks[*track.outputBus], track, 0, block.frames, 1.0F, 1.0F);
            }
        }
        for (std::vector<float>& channel : track.mix) {
            std::fill_n(channel.data(), block.frames, 0.0F);
        }
    }
}

// A take that began after a count-in inside this block records from recordFrom on.
void monitorAndRecordAudio(Snapshot& snapshot, const AudioBlock& block, Frames recordFrom) {
    const bool soloActive = anySolo(snapshot);
    for (TrackState& track : snapshot.tracks) {
        if (track.kind != TrackKind::audio || track.inputs.empty()) {
            continue;
        }
        for (std::size_t index = 0; index < track.inputs.size(); ++index) {
            track.inputPointers[index] = block.inputs[static_cast<std::size_t>(track.inputs[index])];
        }
        if (track.inputMeter) {
            for (std::size_t index = 0; index < track.inputPointers.size(); ++index) {
                raise(*track.inputMeter, index, peakOf(track.inputPointers[index], block.frames));
            }
        }
        if (track.monitor && audible(track, soloActive)) {
            for (std::size_t out = 0; out < track.mix.size(); ++out) {
                const float* source = track.inputPointers[std::min(out, track.inputPointers.size() - 1)];
                float* destination = track.mix[out].data();
                for (Frames index = 0; index < block.frames; ++index) {
                    destination[index] += source[index];
                }
            }
        }
        if (track.recorder && track.recorder->recording()) {
            for (const float*& pointer : track.inputPointers) {
                pointer += recordFrom;
            }
            track.recorder->writeAudio(track.inputPointers, block.frames - recordFrom);
        }
    }
}

// Clicks the count-in through the block and starts the transport where it ends; returns that offset, or 0 when the
// transport was not counting in.
Frames countIn(RealtimeState& state, Snapshot& snapshot, const AudioBlock& block) {
    if (state.countInLeft == 0) {
        return 0;
    }
    const Frames frames = std::min(state.countInLeft, block.frames);
    playCountIn(state, snapshot, block, snapshot.metronome.countInFrames - state.countInLeft, frames);
    state.countInLeft -= frames;
    if (state.countInLeft > 0) {
        return block.frames;
    }
    startTransport(state, snapshot, block.hostTimeNs + framesToNs(frames, snapshot.sampleRate));
    return frames;
}

// The stretch of a block from offset on for frames frames, for the parts that only send MIDI.
AudioBlock midiPart(const Snapshot& snapshot, const AudioBlock& block, Frames offset, Frames frames) {
    return {.frames = frames,
            .hostTimeNs = block.hostTimeNs + framesToNs(offset, snapshot.sampleRate),
            .inputs = {},
            .outputs = {}};
}

// Plays the transport from offset on for frames frames of the block, where the timeline is at state.frame.
void playPart(RealtimeState& state, Snapshot& snapshot, const AudioBlock& block, Frames offset, Frames frames) {
    const AudioBlock midi = midiPart(snapshot, block, offset, frames);
    playMidiClips(snapshot, state.frame, midi);
    sendPendingStart(state, snapshot, midi.hostTimeNs);
    sendClock(snapshot, state.frame, midi);
    playAudioClips(snapshot, state.frame, offset, frames);
    AudioBlock upTo = block;
    upTo.frames = offset + frames;
    playMetronome(state, snapshot, upTo, offset, state.frame);
}

// A preloaded clip waits at the preload's end, so its stream reads ahead from there while the preload plays.
void jumpToCycleStart(RealtimeState& state, Snapshot& snapshot, std::int64_t timestampNs) {
    sendToClockPorts(snapshot, core::midi::kStop, timestampNs);
    releaseNotes(snapshot, timestampNs);
    for (const RestartMessage& message : snapshot.cycleRestart) {
        message.port->queue(message.bytes, timestampNs);
    }
    state.frame = snapshot.cycleStart;
    for (TrackState& track : snapshot.tracks) {
        for (ClipPlayback& clip : track.clips) {
            const Frames local = std::clamp<Frames>(state.frame - clip.start, 0, clip.length);
            clip.stream->seek(clip.preload && clip.preload->start == local ? local + clip.preload->frames : local);
        }
    }
    chaseMidi(snapshot, state.frame, timestampNs);
    state.startPending = true;
}

// Plays the rolling part of the block from offset on and jumps back at each cycle end it crosses, unless we record,
// since a take runs straight on. Returns where the last jump left the timeline in the block, if it jumped.
BlockTime playTransport(RealtimeState& state, Snapshot& snapshot, const AudioBlock& block, Frames offset) {
    BlockTime time{.frame = state.frame, .rollFrom = offset, .playing = true};
    const bool cycling = snapshot.cycleEnd > snapshot.cycleStart && !state.recordEnabled;
    while (offset < block.frames) {
        const Frames left = block.frames - offset;
        const bool jumps = cycling && state.frame < snapshot.cycleEnd && snapshot.cycleEnd - state.frame <= left;
        const Frames frames = jumps ? snapshot.cycleEnd - state.frame : left;
        playPart(state, snapshot, block, offset, frames);
        offset += frames;
        state.frame += frames;
        if (jumps) {
            jumpToCycleStart(state, snapshot, block.hostTimeNs + framesToNs(offset, snapshot.sampleRate));
            time.jumpOffset = offset;
            time.jumpFrame = state.frame;
        }
    }
    return time;
}

}  // namespace

float fadeGain(Frames local, Frames length, Frames fadeIn, Frames fadeOut, core::FadeCurve inCurve,
               core::FadeCurve outCurve) {
    float gain = 1.0F;
    if (fadeIn > 0 && local < fadeIn) {
        gain = core::fadeCurveGain(inCurve, static_cast<float>(local) / static_cast<float>(fadeIn));
    }
    if (fadeOut > 0 && local >= length - fadeOut) {
        gain = std::min(
            gain, core::fadeCurveGain(outCurve, static_cast<float>(length - local) / static_cast<float>(fadeOut)));
    }
    return std::clamp(gain, 0.0F, 1.0F);
}

StereoGains panGains(float pan, PanLaw law) {
    pan = std::isnan(pan) ? 0.0F : std::clamp(pan, -1.0F, 1.0F);
    const float angle = (pan + 1.0F) * std::numbers::pi_v<float> / 4.0F;
    const StereoGains power{.left = std::cos(angle), .right = std::sin(angle)};
    const StereoGains linear{.left = (1.0F - pan) / 2.0F, .right = (1.0F + pan) / 2.0F};
    switch (law) {
        case PanLaw::compensated:
            return {.left = static_cast<float>(std::numbers::sqrt2 * std::cos(static_cast<double>(angle))),
                    .right = static_cast<float>(std::numbers::sqrt2 * std::sin(static_cast<double>(angle)))};
        case PanLaw::zeroDb:
            return {.left = std::min(1.0F - pan, 1.0F), .right = std::min(1.0F + pan, 1.0F)};
        case PanLaw::constantPower:
            return power;
        case PanLaw::compromise:
            return {.left = std::sqrt(power.left * linear.left), .right = std::sqrt(power.right * linear.right)};
        case PanLaw::linear:
            return linear;
    }
    return power;
}

void processBlock(RealtimeState& state, const AudioBlock& block) {
    for (float* output : block.outputs) {
        std::fill_n(output, block.frames, 0.0F);
    }
    adoptPending(state);
    if (state.current == nullptr) {
        return;
    }
    Snapshot& snapshot = *state.current;
    if (snapshot.metronome.sounds.get() != state.voiceSounds) {
        state.voiceSounds = snapshot.metronome.sounds.get();
        state.metronomeVoices = {};
    }
    sendOutgoingMidi(state, snapshot, block.hostTimeNs);
    Command command;
    while (state.commands.try_dequeue(command)) {
        sendOutgoingMidi(state, snapshot, block.hostTimeNs);
        apply(state, snapshot, command, block);
    }
    const Frames rollFrom = countIn(state, snapshot, block);
    receiveMidi(snapshot, state, block, state.frame - rollFrom);
    BlockTime time{.frame = state.frame, .rollFrom = rollFrom, .playing = false};
    if (state.transportPlaying) {
        time = playTransport(state, snapshot, block, rollFrom);
    } else if (snapshot.clockWhileStopped) {
        sendClock(snapshot, state.stoppedClockFrame, block);
        state.stoppedClockFrame += block.frames;
    }
    renderMetronome(state, snapshot, block);
    monitorAndRecordAudio(snapshot, block, rollFrom);
    mixTracks(snapshot, block, time);
    state.publishedFrame.store(state.frame, std::memory_order_relaxed);
    state.publishedPlaying.store(state.transportPlaying || state.countInLeft > 0, std::memory_order_relaxed);
}

}  // namespace emerick::engine
