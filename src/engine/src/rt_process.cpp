// Everything here runs on the audio thread, so it works only on preallocated memory and lock-free queues.

#include "rt_process.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

#include "clock.hpp"
#include "emerick/core/midi.hpp"
#include "emerick/engine/engine.hpp"
#include "rt_metronome.hpp"
#include "rt_midi.hpp"

namespace emerick::engine {

namespace {

// A rebuilt snapshot starts each track from the gains the old one played, so an edit ramps to its new gain.
void keepOutputGains(const Snapshot& from, Snapshot& to) {
    for (TrackState& track : to.tracks) {
        const auto old = std::ranges::find(from.tracks, track.id, &TrackState::id);
        if (old != from.tracks.end() && old->outputGains.size() == track.outputGains.size()) {
            std::ranges::copy(old->outputGains, track.outputGains.begin());
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
                track.recorder->begin(startTick);
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
    updateTake(state, snapshot);
    state.locatesApplied.fetch_add(1, std::memory_order_release);
}

void startTransport(RealtimeState& state, Snapshot& snapshot, std::int64_t timestampNs) {
    state.transportPlaying = true;
    sendToClockPorts(snapshot, core::midi::kStart, timestampNs);
    updateTake(state, snapshot);
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
        if (track.outputs.empty() || !audible(track, soloActive)) {
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
            const std::span<const float> samples = clip.stream->read(local, count);
            const auto channels = static_cast<std::size_t>(clip.stream->channels());
            const auto first = static_cast<std::size_t>(offset + (from - frame));
            for (Frames at = 0; at < count; ++at) {
                if (overlapped && covered(later, from + at)) {
                    continue;
                }
                const float gain = clip.gain * fadeGain(local + at, clip.length, clip.fadeIn, clip.fadeOut,
                                                        clip.fadeInCurve, clip.fadeOutCurve);
                const auto frameIndex = static_cast<std::size_t>(at);
                for (std::size_t out = 0; out < track.outputs.size(); ++out) {
                    track.mix[out][first + frameIndex] +=
                        samples[frameIndex * channels + std::min(out, channels - 1)] * gain;
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

// Applies each track's fader and pan to its mix, meters it, adds it to its outputs and clears it for the next block.
void mixTracks(Snapshot& snapshot, const AudioBlock& block) {
    if (block.frames == 0) {
        return;
    }
    for (TrackState& track : snapshot.tracks) {
        for (std::size_t out = 0; out < track.mix.size(); ++out) {
            float* mixed = track.mix[out].data();
            rampGain(mixed, block.frames, track.outputGains[out], outputGain(track, out, snapshot.panLaw));
            float* destination = block.outputs[static_cast<std::size_t>(track.outputs[out])];
            for (Frames index = 0; index < block.frames; ++index) {
                destination[index] += mixed[index];
            }
            raise(*track.outputMeter, out, peakOf(mixed, block.frames));
            std::fill_n(mixed, block.frames, 0.0F);
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

AudioBlock tailOf(Snapshot& snapshot, const AudioBlock& block, Frames offset) {
    for (std::size_t index = 0; index < block.outputs.size(); ++index) {
        snapshot.tailOutputs[index] = block.outputs[index] + offset;
    }
    return {.frames = block.frames - offset,
            .hostTimeNs = block.hostTimeNs + framesToNs(offset, snapshot.sampleRate),
            .inputs = {},
            .outputs = std::span(snapshot.tailOutputs).first(block.outputs.size())};
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
    Command command;
    while (state.commands.try_dequeue(command)) {
        apply(state, snapshot, command, block);
    }
    const Frames rollFrom = countIn(state, snapshot, block);
    receiveMidi(snapshot, state, block, state.frame - rollFrom);
    if (state.transportPlaying) {
        const AudioBlock rolling = rollFrom == 0 ? block : tailOf(snapshot, block, rollFrom);
        sendClock(snapshot, state.frame, rolling);
        playMidiClips(snapshot, state.frame, rolling);
        playAudioClips(snapshot, state.frame, rollFrom, rolling.frames);
        playMetronome(state, snapshot, block, rollFrom, state.frame);
    }
    renderMetronome(state, snapshot, block);
    monitorAndRecordAudio(snapshot, block, rollFrom);
    mixTracks(snapshot, block);
    if (state.transportPlaying) {
        state.frame += block.frames - rollFrom;
    }
    state.publishedFrame.store(state.frame, std::memory_order_relaxed);
    state.publishedPlaying.store(state.transportPlaying || state.countInLeft > 0, std::memory_order_relaxed);
}

}  // namespace emerick::engine
