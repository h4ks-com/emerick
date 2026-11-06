// Everything here runs on the audio thread, so it works only on preallocated memory and lock-free queues.

#include "rt_process.hpp"

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <functional>
#include <iterator>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <stop_token>
#include <utility>

#include "clock.hpp"
#include "emerick/core/midi.hpp"
#include "emerick/engine/engine.hpp"
#include "render_pipeline.hpp"
#include "rt_metronome.hpp"
#include "rt_midi.hpp"

#ifdef __APPLE__
#include <pthread/qos.h>
#endif

namespace emerick::engine {

namespace {

// A rebuilt snapshot starts each track from the gains and trim the old one played, so an edit ramps to its new value.
void keepRampState(const Snapshot& from, Snapshot& to) {
    for (TrackState& track : to.tracks) {
        const auto old = std::ranges::find(from.tracks, track.id, &TrackState::id);
        if (old != from.tracks.end()) {
            track.trimFrom = old->trimFrom;
        }
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
    next->pipelined = state.pipeline != nullptr;
    if (state.current != nullptr) {
        keepRampState(*state.current, *next);
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
    if (std::exchange(state.startPending, false) && snapshot.sendStart) {
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
        case Command::Type::previewMetronome:
            state.metronomePreviewLeft =
                snapshot.tempo.ticksToFrames(kMetronomePreviewBars * snapshot.tempo.ticksPerBar(), snapshot.sampleRate);
            break;
        case Command::Type::stop:
            state.countInLeft = 0;
            state.metronomePreviewLeft = 0;
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

void applyTrim(TrackState& track, Frames frames) {
    if (track.trim == 1.0F && track.trimFrom == 1.0F) {
        return;
    }
    for (std::vector<float>& channel : track.mix) {
        float from = track.trimFrom;
        rampGain(channel.data(), frames, from, track.trim);
    }
    track.trimFrom = track.trim;
}

void addRamped(float* destination, const float* source, Frames frames, float from, float to) {
    const float step = (to - from) / static_cast<float>(frames);
    for (Frames index = 0; index < frames; ++index) {
        destination[index] += source[index] * (from + (step * static_cast<float>(index + 1)));
    }
}

// A track with fewer channels than the bus feeds its last channel to the rest, so a mono track reaches both sides.
// While an offline render pipelines, the tracks feed the next block of the buses mixing behind them.
void feedBus(const Snapshot& snapshot, TrackState& bus, const TrackState& track, Frames offset, Frames frames,
             float from, float to) {
    const auto first = static_cast<std::size_t>(offset);
    std::vector<std::vector<float>>& input =
        snapshot.pipelined && !track.mixesBehind && bus.mixesBehind ? bus.pending : bus.mix;
    for (std::size_t channel = 0; channel < input.size(); ++channel) {
        addRamped(input[channel].data() + first, track.mix[std::min(channel, track.mix.size() - 1)].data() + first,
                  frames, from, to);
    }
}

void playSends(Snapshot& snapshot, TrackState& track, bool pre, Frames offset, Frames frames) {
    for (SendState& send : track.sends) {
        if (send.pre == pre) {
            const float gain = send.gain * track.muteGain;
            feedBus(snapshot, snapshot.tracks[send.bus], track, offset, frames, send.rampFrom, gain);
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

// A plugin takes one value per lane at the start of each block, so later parts of the block leave its params alone.
void applyLanes(TrackState& track, Frames frame, bool blockStart) {
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
                if (InsertState& insert = track.inserts[lane.insert]; !insert.plugin) {
                    insert.params.at(lane.param) = value;
                } else if (blockStart) {
                    insert.plugin->queueParam(static_cast<std::uint32_t>(lane.param), value);
                }
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

// In mix order, so each bus has every source in its mix before its turn: trims each track, runs its inserts, applies
// its fader and pan, feeds its sends, meters it, adds it to its outputs or its output bus and clears it for the next
// block.
void mixTracks(Snapshot& snapshot, const AudioBlock& block, const BlockTime& time, const InstrumentTransport& transport,
               std::size_t from, std::size_t to) {
    if (block.frames == 0) {
        return;
    }
    const bool soloActive = anySolo(snapshot);
    for (const std::size_t index : std::span{snapshot.mixOrder}.subspan(from, to - from)) {
        TrackState& track = snapshot.tracks[index];
        if (!track.mix.empty() && audible(track, soloActive)) {
            if (!track.lanes.empty()) {
                applyLanes(track, time.at(block.frames), true);
            }
            applyTrim(track, block.frames);
            for (InsertState& insert : track.inserts) {
                if (insert.effect) {
                    insert.effect->process(track.mixPointers, block.frames, insert.params, insert.bypass,
                                           snapshot.tempo.bpm);
                } else if (!insert.bypass) {
                    insert.plugin->processEffect(track.mixPointers, block.frames, block.hostTimeNs, transport);
                }
            }
            if (track.compensation) {
                track.compensation->process(track.mixPointers, block.frames);
            }
            for (std::size_t channel = 0; channel < track.mix.size() && channel < track.chainCaptures.size();
                 ++channel) {
                const float* chain = track.mix[channel].data();
                float* destination = block.outputs[static_cast<std::size_t>(track.chainCaptures[channel])];
                std::transform(chain, chain + block.frames, destination, destination, std::plus{});
            }
            const Frames step = track.lanes.empty() ? block.frames : kAutomationStep;
            for (Frames offset = 0; offset < block.frames; offset += step) {
                const Frames frames = std::min(step, block.frames - offset);
                applyLanes(track, time.at(offset + frames), false);
                mixPart(snapshot, track, offset, frames);
            }
            for (std::size_t channel = 0; channel < track.mix.size(); ++channel) {
                const float* mixed = track.mix[channel].data();
                raise(*track.outputMeter, channel, peakOf(mixed, block.frames));
                if (channel < track.outputs.size()) {
                    float* destination = block.outputs[static_cast<std::size_t>(track.outputs[channel])];
                    std::transform(mixed, mixed + block.frames, destination, destination, std::plus{});
                }
                if (channel < track.captures.size()) {
                    float* destination = block.outputs[static_cast<std::size_t>(track.captures[channel])];
                    std::transform(mixed, mixed + block.frames, destination, destination, std::plus{});
                }
            }
            for (const std::shared_ptr<SignalTap>& tap : track.outputTaps) {
                tap->ring.writeMean(track.mixPointers, static_cast<std::size_t>(block.frames));
            }
            if (track.outputBus) {
                feedBus(snapshot, snapshot.tracks[*track.outputBus], track, 0, block.frames, 1.0F, 1.0F);
            }
        }
        for (std::vector<float>& channel : track.mix) {
            std::fill_n(channel.data(), block.frames, 0.0F);
        }
    }
}

double quarters(const Snapshot& snapshot, Frames frame) {
    return static_cast<double>(frame) / snapshot.sampleRate * snapshot.tempo.bpm / 60.0;
}

// A count-in that ends inside the block starts the transport for the instruments with the next block.
InstrumentTransport instrumentTransport(const RealtimeState& state, const Snapshot& snapshot, const BlockTime& time) {
    const bool looping = snapshot.cycleEnd > snapshot.cycleStart && !state.recordEnabled;
    return {.playing = time.playing && time.rollFrom == 0,
            .beats = quarters(snapshot, time.frame),
            .bpm = snapshot.tempo.bpm,
            .meter = snapshot.tempo.meter,
            .looping = looping,
            .loopStartBeats = looping ? quarters(snapshot, snapshot.cycleStart) : 0.0,
            .loopEndBeats = looping ? quarters(snapshot, snapshot.cycleEnd) : 0.0};
}

// Each instrument plays into its track's mix after the track it takes MIDI from, so that MIDI reaches it in this block.
void playInstruments(const RealtimeState& state, Snapshot& snapshot, const AudioBlock& block, Frames blockStartFrame,
                     const BlockTime& time) {
    const bool soloActive = anySolo(snapshot);
    const InstrumentTransport transport = instrumentTransport(state, snapshot, time);
    for (const std::size_t index : snapshot.instrumentOrder) {
        TrackState& track = snapshot.tracks[index];
        if (audible(track, soloActive)) {
            track.instrument->processBlock(track.mixPointers, block.frames, block.hostTimeNs, transport);
            routeInstrumentMidi(snapshot, state, index, block, blockStartFrame);
        }
    }
}

void adoptPreview(RealtimeState& state) {
    if (state.pendingPreview.load(std::memory_order_acquire) == nullptr ||
        state.retiredPreviews.size_approx() >= state.retiredPreviews.max_capacity()) {
        return;
    }
    state.previewSounding.store(true, std::memory_order_release);
    PreviewSound* next = state.pendingPreview.exchange(nullptr, std::memory_order_acq_rel);
    if (state.preview != nullptr) {
        state.retiredPreviews.try_enqueue(state.preview);
    }
    state.preview = next;
}

void playPreview(RealtimeState& state, Snapshot& snapshot, Frames frames) {
    adoptPreview(state);
    PreviewSound* preview = state.preview;
    if (preview == nullptr) {
        return;
    }
    const auto length = static_cast<Frames>(preview->channels.front().size());
    const Frames count = std::min(frames, length - preview->position);
    if (TrackState* track = findTrack(snapshot, preview->trackId); track != nullptr) {
        std::vector<std::vector<float>>& input = snapshot.pipelined && track->mixesBehind ? track->pending : track->mix;
        for (std::size_t channel = 0; channel < input.size(); ++channel) {
            const float* source =
                preview->channels[std::min(channel, preview->channels.size() - 1)].data() + preview->position;
            std::transform(source, source + count, input[channel].data(), input[channel].data(), std::plus{});
        }
    }
    preview->position += count;
    if (preview->position >= length && state.retiredPreviews.try_enqueue(preview)) {
        state.preview = nullptr;
        if (state.pendingPreview.load(std::memory_order_acquire) == nullptr) {
            state.previewSounding.store(false, std::memory_order_release);
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
        for (const std::shared_ptr<SignalTap>& tap : track.inputTaps) {
            tap->ring.writeMean(track.inputPointers, static_cast<std::size_t>(block.frames));
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

// Clicks the metronome preview through the block while the transport rests; playing or counting in ends it.
void previewMetronome(RealtimeState& state, const Snapshot& snapshot, const AudioBlock& block) {
    if (state.transportPlaying || state.countInLeft > 0) {
        state.metronomePreviewLeft = 0;
    }
    if (state.metronomePreviewLeft > 0) {
        const Frames total =
            snapshot.tempo.ticksToFrames(kMetronomePreviewBars * snapshot.tempo.ticksPerBar(), snapshot.sampleRate);
        const Frames frames = std::min(state.metronomePreviewLeft, block.frames);
        playMetronomePreview(state, snapshot, block, total - state.metronomePreviewLeft, frames);
        state.metronomePreviewLeft -= frames;
    }
    if (state.metronomePreviewLeft == 0) {
        state.metronomePreviewing.store(false, std::memory_order_release);
    }
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
        if (message.port) {
            message.port->queue(message.bytes, timestampNs);
        } else {
            message.instrument->queueMidi(message.bytes, timestampNs);
        }
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

namespace {

std::optional<std::pair<BlockTime, InstrumentTransport>> playBlock(RealtimeState& state, const AudioBlock& block,
                                                                   bool tracksOnly) {
    for (float* output : block.outputs) {
        std::fill_n(output, block.frames, 0.0F);
    }
    adoptPending(state);
    if (state.current == nullptr) {
        return std::nullopt;
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
    previewMetronome(state, snapshot, block);
    const Frames blockStartFrame = state.frame - rollFrom;
    receiveMidi(snapshot, state, block, blockStartFrame);
    BlockTime time{.frame = state.frame, .rollFrom = rollFrom, .playing = false};
    if (state.transportPlaying) {
        time = playTransport(state, snapshot, block, rollFrom);
    } else if (snapshot.clockWhileStopped) {
        sendClock(snapshot, state.stoppedClockFrame, block);
        state.stoppedClockFrame += block.frames;
    }
    renderMetronome(state, snapshot, block);
    monitorAndRecordAudio(snapshot, block, rollFrom);
    playInstruments(state, snapshot, block, blockStartFrame, time);
    playPreview(state, snapshot, block.frames);
    const InstrumentTransport transport = instrumentTransport(state, snapshot, time);
    mixTracks(snapshot, block, time, transport, 0, tracksOnly ? snapshot.busesFrom : snapshot.mixOrder.size());
    return std::pair{time, transport};
}

void publish(RealtimeState& state, const AudioBlock& block) {
    if (state.current != nullptr) {
        for (const std::shared_ptr<SignalTap>& tap : state.current->masterTaps) {
            tap->ring.writeMean(block.outputs.first(std::min<std::size_t>(block.outputs.size(), 2)),
                                static_cast<std::size_t>(block.frames));
        }
    }
    state.publishedFrame.store(state.frame, std::memory_order_relaxed);
    state.publishedPlaying.store(state.transportPlaying || state.countInLeft > 0, std::memory_order_relaxed);
}

// A command, a new snapshot or a preview changes what the buses read, so the buses of the last block finish first.
bool changesBuses(const RealtimeState& state) {
    return state.commands.size_approx() > 0 || state.pending.load(std::memory_order_acquire) != nullptr ||
           state.preview != nullptr || state.pendingPreview.load(std::memory_order_acquire) != nullptr;
}

}  // namespace

void processBlock(RealtimeState& state, const AudioBlock& block) {
    RenderPipeline* pipeline = state.pipeline;
    if (pipeline == nullptr || block.frames == 0) {
        if (pipeline != nullptr) {
            pipeline->finish();
        }
        playBlock(state, block, false);
        publish(state, block);
        return;
    }
    if (changesBuses(state)) {
        pipeline->finish();
    }
    AudioBlock tracks = block;
    tracks.outputs = pipeline->trackOutputs();
    const auto played = playBlock(state, tracks, true);
    pipeline->finish();
    if (played) {
        pipeline->start(*state.current, tracks, played->first, played->second);
    } else {
        pipeline->queueSilence(block.frames);
    }
    pipeline->deliver(block.outputs, block.frames);
    publish(state, block);
}

RenderPipeline::RenderPipeline() : busThread_([this](const std::stop_token& stop) { mixBuses(stop); }) {}

// NOLINTNEXTLINE(bugprone-exception-escape): a semaphore that cannot wait ends the process.
RenderPipeline::~RenderPipeline() {
    wait();
    busThread_.request_stop();
    started_.release();
}

void RenderPipeline::prepare(std::size_t outputs, Frames maxFrames) {
    finish();
    maxFrames_ = maxFrames;
    const auto frames = static_cast<std::size_t>(maxFrames);
    trackBuffers_.assign(outputs, std::vector<float>(frames));
    busBuffers_.assign(outputs, std::vector<float>(frames));
    const auto pointers = [](std::vector<std::vector<float>>& buffers) {
        return buffers | std::views::transform([](std::vector<float>& buffer) { return buffer.data(); }) |
               std::ranges::to<std::vector>();
    };
    trackPointers_ = pointers(trackBuffers_);
    busPointers_ = pointers(busBuffers_);
    queue_.assign(outputs, std::vector<float>(frames));
    for (std::vector<float>& channel : queue_) {
        channel.reserve(3 * frames);
    }
}

std::span<float* const> RenderPipeline::trackOutputs() { return trackPointers_; }

void RenderPipeline::queueSilence(Frames frames) {
    for (std::vector<float>& channel : queue_) {
        channel.resize(channel.size() + static_cast<std::size_t>(frames));
    }
}

void RenderPipeline::wait() {
    if (mixing_) {
        finished_.acquire();
        mixing_ = false;
    }
}

void RenderPipeline::finish() {
    if (!mixing_) {
        return;
    }
    wait();
    const auto frames = static_cast<std::size_t>(busBlock_.block.frames);
    for (std::size_t channel = 0; channel < queue_.size(); ++channel) {
        queue_[channel].insert(queue_[channel].end(), busBuffers_[channel].begin(),
                               busBuffers_[channel].begin() + static_cast<std::ptrdiff_t>(frames));
    }
}

void RenderPipeline::start(Snapshot& snapshot, const AudioBlock& block, const BlockTime& time,
                           const InstrumentTransport& transport) {
    for (TrackState& track : snapshot.tracks) {
        if (track.mixesBehind) {
            std::swap(track.mix, track.pending);
            std::swap(track.mixPointers, track.pendingPointers);
        }
    }
    std::swap(trackBuffers_, busBuffers_);
    std::swap(trackPointers_, busPointers_);
    std::fegetenv(&environment_);
    busBlock_ = {
        .snapshot = &snapshot,
        .block = {.frames = block.frames, .hostTimeNs = block.hostTimeNs, .inputs = {}, .outputs = busPointers_},
        .time = time,
        .transport = transport};
    mixing_ = true;
    started_.release();
}

void RenderPipeline::deliver(std::span<float* const> outputs, Frames frames) {
    const auto count = static_cast<std::ptrdiff_t>(frames);
    for (std::size_t channel = 0; channel < queue_.size() && channel < outputs.size(); ++channel) {
        std::vector<float>& queued = queue_[channel];
        std::copy_n(queued.begin(), count, outputs[channel]);
        queued.erase(queued.begin(), queued.begin() + count);
    }
}

void RenderPipeline::mixBuses(const std::stop_token& stop) {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif
    for (;;) {
        started_.acquire();
        if (stop.stop_requested()) {
            return;
        }
        std::fesetenv(&environment_);
        mixTracks(*busBlock_.snapshot, busBlock_.block, busBlock_.time, busBlock_.transport,
                  busBlock_.snapshot->busesFrom, busBlock_.snapshot->mixOrder.size());
        finished_.release();
    }
}

}  // namespace emerick::engine
