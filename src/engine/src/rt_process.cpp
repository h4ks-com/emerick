// Everything here runs on the audio thread, so it works only on preallocated memory and lock-free queues.

#include "rt_process.hpp"

#include <algorithm>

#include "emerick/core/midi.hpp"
#include "emerick/engine/engine.hpp"
#include "rt_midi.hpp"

namespace emerick::engine {

namespace {

void adoptPending(RealtimeState& state) {
    if (state.pending.load(std::memory_order_acquire) == nullptr) {
        return;
    }
    if (state.current != nullptr && state.retired.size_approx() >= state.retired.max_capacity()) {
        return;
    }
    Snapshot* next = state.pending.exchange(nullptr, std::memory_order_acq_rel);
    if (state.current != nullptr) {
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

void apply(RealtimeState& state, Snapshot& snapshot, const Command& command, const AudioBlock& block) {
    TrackState* track = findTrack(snapshot, command.track());
    switch (command.type) {
        case Command::Type::play:
            if (!state.transportPlaying) {
                state.transportPlaying = true;
                sendToClockPorts(snapshot, core::midi::kStart, block.hostTimeNs);
                updateTake(state, snapshot);
            }
            break;
        case Command::Type::stop:
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
    }
}

void playAudioClips(const Snapshot& snapshot, Frames frame, const AudioBlock& block) {
    const bool soloActive = anySolo(snapshot);
    const Frames blockEnd = frame + block.frames;
    for (const TrackState& track : snapshot.tracks) {
        if (track.outputs.empty() || !audible(track, soloActive)) {
            continue;
        }
        for (const ClipPlayback& clip : track.clips) {
            const Frames from = std::max(frame, clip.start);
            const Frames to = std::min(blockEnd, clip.start + clip.length);
            if (from >= to) {
                continue;
            }
            const Frames local = from - clip.start;
            const Frames count = to - from;
            const std::span<const float> samples = clip.stream->read(local, count);
            const auto channels = static_cast<std::size_t>(clip.stream->channels());
            for (std::size_t out = 0; out < track.outputs.size(); ++out) {
                float* destination = block.outputs[static_cast<std::size_t>(track.outputs[out])] + (from - frame);
                const std::size_t source = std::min(out, channels - 1);
                for (Frames index = 0; index < count; ++index) {
                    const float gain =
                        track.gain * clip.gain * fadeGain(local + index, clip.length, clip.fadeIn, clip.fadeOut);
                    destination[index] += samples[static_cast<std::size_t>(index) * channels + source] * gain;
                }
            }
        }
    }
}

void monitorAndRecordAudio(Snapshot& snapshot, const AudioBlock& block) {
    const bool soloActive = anySolo(snapshot);
    for (TrackState& track : snapshot.tracks) {
        if (track.kind != TrackKind::audio || track.inputs.empty()) {
            continue;
        }
        for (std::size_t index = 0; index < track.inputs.size(); ++index) {
            track.inputPointers[index] = block.inputs[static_cast<std::size_t>(track.inputs[index])];
        }
        if (track.recorder && track.recorder->recording()) {
            track.recorder->writeAudio(track.inputPointers, block.frames);
        }
        if (!track.monitor || !audible(track, soloActive)) {
            continue;
        }
        for (std::size_t out = 0; out < track.outputs.size(); ++out) {
            const float* source = track.inputPointers[std::min(out, track.inputPointers.size() - 1)];
            float* destination = block.outputs[static_cast<std::size_t>(track.outputs[out])];
            for (Frames index = 0; index < block.frames; ++index) {
                destination[index] += source[index] * track.gain;
            }
        }
    }
}

}  // namespace

float fadeGain(Frames local, Frames length, Frames fadeIn, Frames fadeOut) {
    float gain = 1.0F;
    if (fadeIn > 0 && local < fadeIn) {
        gain = static_cast<float>(local) / static_cast<float>(fadeIn);
    }
    if (fadeOut > 0 && local >= length - fadeOut) {
        gain = std::min(gain, static_cast<float>(length - local) / static_cast<float>(fadeOut));
    }
    return std::clamp(gain, 0.0F, 1.0F);
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
    Command command;
    while (state.commands.try_dequeue(command)) {
        apply(state, snapshot, command, block);
    }
    receiveMidi(snapshot, state, block);
    if (state.transportPlaying) {
        sendClock(snapshot, state.frame, block);
        playMidiClips(snapshot, state.frame, block);
        playAudioClips(snapshot, state.frame, block);
    }
    monitorAndRecordAudio(snapshot, block);
    if (state.transportPlaying) {
        state.frame += block.frames;
    }
    state.publishedFrame.store(state.frame, std::memory_order_relaxed);
    state.publishedPlaying.store(state.transportPlaying, std::memory_order_relaxed);
}

}  // namespace emerick::engine
