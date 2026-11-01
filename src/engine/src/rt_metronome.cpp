// Everything here runs on the audio thread, so it works only on preallocated memory and lock-free queues.

#include "rt_metronome.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "clock.hpp"
#include "emerick/core/midi.hpp"

namespace emerick::engine {

namespace {

constexpr float kSoftHit = 0.55F;
constexpr float kBaseVelocity = 80.0F;
constexpr long kMaxVelocity = 127;
constexpr int kDrumChannel = 10;
constexpr std::int64_t kNoteLengthNs = 30'000'000;

bool sendsMidi(MetronomeMode mode) { return mode == MetronomeMode::midiClick || mode == MetronomeMode::midiGroove; }

bool playsGroove(MetronomeMode mode) { return mode == MetronomeMode::groove || mode == MetronomeMode::midiGroove; }

void sendNote(const MetronomeState& metronome, std::uint8_t note, float gain, std::int64_t timestampNs) {
    const int channel = metronome.settings.midiOut.channel == 0 ? kDrumChannel : metronome.settings.midiOut.channel;
    const auto velocity = static_cast<std::uint8_t>(std::clamp(std::lround(kBaseVelocity * gain), 1L, kMaxVelocity));
    const std::array on{core::midi::statusFor(core::midi::kNoteOn, channel), note, velocity};
    const std::array off{core::midi::statusFor(core::midi::kNoteOff, channel), note, std::uint8_t{0}};
    metronome.midiOut->queue(on, timestampNs);
    metronome.midiOut->queue(off, timestampNs + kNoteLengthNs);
}

void trigger(RealtimeState& state, const Snapshot& snapshot, const AudioBlock& block, DrumVoice voice, float gain,
             Frames offset) {
    const MetronomeState& metronome = snapshot.metronome;
    if (sendsMidi(metronome.settings.mode)) {
        if (metronome.midiOut) {
            sendNote(metronome, metronome.settings.notes[static_cast<std::size_t>(voice)], gain,
                     block.hostTimeNs + framesToNs(offset, snapshot.sampleRate));
        }
        return;
    }
    auto* slot = std::ranges::find(state.metronomeVoices, false, &MetronomeVoice::active);
    if (slot == state.metronomeVoices.end()) {
        slot = std::ranges::max_element(state.metronomeVoices, {}, &MetronomeVoice::position);
    }
    *slot = {.voice = voice, .position = -offset, .gain = gain, .active = true};
}

// Calls hit with every step of ticksPerStep whose frame lies in [from, to).
void forEachStep(const core::TempoMap& tempo, double sampleRate, Ticks ticksPerStep, Frames from, Frames to, auto hit) {
    for (Ticks step = std::max<Ticks>(0, tempo.framesToTicks(from, sampleRate) / ticksPerStep - 1);; ++step) {
        const Frames at = tempo.ticksToFrames(step * ticksPerStep, sampleRate);
        if (at >= to) {
            break;
        }
        if (at >= from) {
            hit(step, at);
        }
    }
}

// Plays the steps over frames frames of the grid from frame from, which sit in the block from offset on.
void playSteps(RealtimeState& state, const Snapshot& snapshot, const AudioBlock& block, Frames offset, Frames from,
               Frames frames, bool groove) {
    const Metronome& settings = snapshot.metronome.settings;
    const Groove& pattern = kGrooves[static_cast<std::size_t>(settings.pattern)];
    const Ticks stepsPerBeat = groove ? pattern.stepsPerBeat : 1;
    const core::TempoMap& tempo = snapshot.tempo;
    forEachStep(
        tempo, snapshot.sampleRate, tempo.ticksPerBeat() / stepsPerBeat, from, from + frames,
        [&](Ticks step, Frames at) {
            const Frames hitOffset = offset + at - from;
            const Ticks beatInBar = step / stepsPerBeat % tempo.meter.numerator;
            const bool accent = step % stepsPerBeat == 0 && std::ranges::contains(settings.accentBeats, beatInBar + 1);
            const float accentGain = accent ? settings.accentGain : 1.0F;
            if (!groove) {
                trigger(state, snapshot, block, accent ? DrumVoice::accent : DrumVoice::beat, accentGain, hitOffset);
                return;
            }
            const auto patternStep =
                static_cast<std::size_t>(beatInBar % kGrooveBeats * stepsPerBeat + step % stepsPerBeat);
            for (std::size_t voice = 0; voice < pattern.voices.size(); ++voice) {
                const char hit = pattern.voices[voice][patternStep];
                if (hit != '.') {
                    trigger(state, snapshot, block,
                            static_cast<DrumVoice>(static_cast<std::size_t>(DrumVoice::kick) + voice),
                            (hit == 'X' ? 1.0F : kSoftHit) * accentGain, hitOffset);
                }
            }
        });
}

}  // namespace

void playCountIn(RealtimeState& state, const Snapshot& snapshot, const AudioBlock& block, Frames done, Frames frames) {
    playSteps(state, snapshot, block, 0, done, frames, false);
}

void playMetronomePreview(RealtimeState& state, const Snapshot& snapshot, const AudioBlock& block, Frames done,
                          Frames frames) {
    playSteps(state, snapshot, block, 0, done, frames, playsGroove(snapshot.metronome.settings.mode));
}

void playMetronome(RealtimeState& state, const Snapshot& snapshot, const AudioBlock& block, Frames offset,
                   Frames frame) {
    const Metronome& settings = snapshot.metronome.settings;
    if (!settings.enabled || !(state.recordEnabled ? settings.duringRecording : settings.duringPlayback)) {
        return;
    }
    playSteps(state, snapshot, block, offset, frame, block.frames - offset, playsGroove(settings.mode));
}

void renderMetronome(RealtimeState& state, const Snapshot& snapshot, const AudioBlock& block) {
    const MetronomeState& metronome = snapshot.metronome;
    if (!metronome.sounds) {
        return;
    }
    for (MetronomeVoice& voice : state.metronomeVoices) {
        if (!voice.active) {
            continue;
        }
        const std::vector<float>& sound = metronome.sounds->voices[static_cast<std::size_t>(voice.voice)];
        const auto length = static_cast<Frames>(sound.size());
        const Frames first = std::max<Frames>(0, -voice.position);
        const Frames last = std::min(block.frames, length - voice.position);
        const float gain = voice.gain * metronome.settings.gain;
        for (const int output : metronome.outputs) {
            float* destination = block.outputs[static_cast<std::size_t>(output)];
            for (Frames index = first; index < last; ++index) {
                destination[index] += sound[static_cast<std::size_t>(voice.position + index)] * gain;
            }
        }
        voice.position += block.frames;
        voice.active = voice.position < length;
    }
}

}  // namespace emerick::engine
