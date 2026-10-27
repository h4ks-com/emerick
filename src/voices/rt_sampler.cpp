// The realtime sampler: every function here runs on the audio thread and never allocates, locks or logs.

#include "rt_sampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace emerick::voices {

namespace {

constexpr float kVelocityScale = 127.0F;

float rampStep(double seconds, double rate) {
    const double frames = seconds * rate;
    return frames < 1.0 ? 1.0F : static_cast<float>(1.0 / frames);
}

double semitonesOf(const Zone& zone, int key, const PlaySettings& settings) {
    const double keyed = zone.fixedPitch ? 0.0 : key + zone.keyShift - zone.rootKey;
    return keyed + zone.tuneCents / 100.0 + settings.tuneSemitones;
}

float sampleAt(const Wave& wave, double position) {
    const auto index = static_cast<std::size_t>(position);
    if (index + 1 >= wave.samples.size()) {
        return index < wave.samples.size() ? wave.samples[index] : 0.0F;
    }
    const auto fraction = static_cast<float>(position - static_cast<double>(index));
    return wave.samples[index] + (fraction * (wave.samples[index + 1] - wave.samples[index]));
}

}  // namespace

Sampler::Note& Sampler::freeNote() {
    auto* const idle = std::ranges::find(notes_, Stage::idle, &Note::stage);
    if (idle != notes_.end()) {
        return *idle;
    }
    // We steal the quietest releasing note, or else the oldest note.
    auto* const releasing = std::ranges::min_element(
        notes_, {}, [](const Note& note) { return note.stage == Stage::release ? note.level : 2.0F; });
    if (releasing->stage == Stage::release) {
        return *releasing;
    }
    return *std::ranges::min_element(notes_, {}, &Note::started);
}

void Sampler::noteOn(const Bank& bank, int key, int velocity, int channel, const PlaySettings& settings) {
    for (const BankZone& zone : bank.zones) {
        const Zone& info = *zone.zone;
        if (key < info.keyLo || key > info.keyHi || velocity < info.velocityLo || velocity > info.velocityHi) {
            continue;
        }
        const float gain = settings.volume * info.gain * (static_cast<float>(velocity) / kVelocityScale);
        Note& note = freeNote();
        note = Note{.stage = Stage::attack,
                    .zone = &zone,
                    .key = key,
                    .channel = channel,
                    .sustained = false,
                    .started = ++clock_,
                    .position = 0.0,
                    .step = info.sampleRate / settings.outputRate * std::exp2(semitonesOf(info, key, settings) / 12.0),
                    .gainLeft = gain * std::min(1.0F, 1.0F - info.pan),
                    .gainRight = gain * std::min(1.0F, 1.0F + info.pan),
                    .level = 0.0F,
                    .attackStep = rampStep(settings.attackSeconds, settings.outputRate),
                    .releaseStep = 0.0F};
    }
}

void Sampler::release(Note& note, const PlaySettings& settings) {
    note.stage = Stage::release;
    note.sustained = false;
    note.releaseStep = note.level * rampStep(settings.releaseSeconds, settings.outputRate);
}

void Sampler::noteOff(int key, int channel, const PlaySettings& settings) {
    for (Note& note : notes_) {
        if ((note.stage == Stage::attack || note.stage == Stage::hold) && note.key == key && note.channel == channel) {
            if (sustain_.at(static_cast<std::size_t>(channel))) {
                note.sustained = true;
            } else {
                release(note, settings);
            }
        }
    }
}

void Sampler::setSustain(int channel, bool down, const PlaySettings& settings) {
    sustain_.at(static_cast<std::size_t>(channel)) = down;
    if (down) {
        return;
    }
    for (Note& note : notes_) {
        if (note.sustained && note.channel == channel) {
            release(note, settings);
        }
    }
}

void Sampler::releaseAll(const PlaySettings& settings) {
    sustain_.fill(false);
    for (Note& note : notes_) {
        if (note.stage != Stage::idle) {
            release(note, settings);
        }
    }
}

void Sampler::silence() {
    sustain_.fill(false);
    for (Note& note : notes_) {
        note.stage = Stage::idle;
    }
}

void Sampler::renderNote(Note& note, std::span<float> left, std::span<float> right) {
    const Wave& leftWave = *note.zone->left;
    const Wave& rightWave = note.zone->right != nullptr ? *note.zone->right : leftWave;
    const bool looped = leftWave.looped() && !note.zone->zone->oneShot;
    const auto loopStart = static_cast<double>(leftWave.loopStart);
    const auto loopEnd = static_cast<double>(leftWave.loopEnd);
    const auto end = static_cast<double>(leftWave.samples.size());
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        if (note.stage == Stage::attack) {
            note.level = std::min(1.0F, note.level + note.attackStep);
            note.stage = note.level >= 1.0F ? Stage::hold : Stage::attack;
        } else if (note.stage == Stage::release) {
            note.level -= note.releaseStep;
        }
        if (note.level <= 0.0F && note.stage == Stage::release) {
            note.stage = Stage::idle;
            return;
        }
        left[frame] += note.gainLeft * note.level * sampleAt(leftWave, note.position);
        right[frame] += note.gainRight * note.level * sampleAt(rightWave, note.position);
        note.position += note.step;
        if (looped) {
            while (note.position >= loopEnd) {
                note.position -= loopEnd - loopStart;
            }
        } else if (note.position >= end) {
            note.stage = Stage::idle;
            return;
        }
    }
}

void Sampler::render(std::span<float> left, std::span<float> right) {
    for (Note& note : notes_) {
        if (note.stage != Stage::idle) {
            renderNote(note, left, right);
        }
    }
}

}  // namespace emerick::voices
