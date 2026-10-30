// The realtime sampler: every function here runs on the audio thread and never allocates, locks or logs.

#include "rt_sampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace emerick::voices {

namespace {

constexpr float kVelocityScale = 127.0F;
constexpr double kEnvelopeFloor = 1.5849e-5;
constexpr double kHighestCutoff = 0.45;
constexpr double kChokeSeconds = 0.005;
constexpr std::size_t kReleaseSegment = 4;

double framesOf(double seconds, double rate) { return std::max(1.0, seconds * rate); }

double ratioOver(double from, double to, double seconds, double rate) {
    return std::pow(to / from, 1.0 / framesOf(seconds, rate));
}

// Multiplies the level by the ratio and reports whether it reached the target.
bool reached(double& level, double ratio, double target) {
    level *= ratio;
    if (ratio < 1.0 ? level <= target : level >= target) {
        level = target;
        return true;
    }
    return false;
}

void tune(Filter& filter, double hertz, double q, double rate) {
    const double g = std::tan(std::numbers::pi * std::min(hertz, kHighestCutoff * rate) / rate);
    if (filter.onePole) {
        filter.a1 = g / (1.0 + g);
        return;
    }
    filter.a1 = 1.0 / (1.0 + (g * (g + (1.0 / q))));
    filter.a2 = g * filter.a1;
    filter.a3 = g * filter.a2;
}

double lowPassed(Filter& filter, std::size_t side, double input) {
    if (filter.onePole) {
        const double v = filter.a1 * (input - filter.ic1.at(side));
        const double output = v + filter.ic1.at(side);
        filter.ic1.at(side) = output + v;
        return output;
    }
    const double v3 = input - filter.ic2.at(side);
    const double v1 = (filter.a1 * filter.ic1.at(side)) + (filter.a2 * v3);
    const double v2 = filter.ic2.at(side) + (filter.a2 * filter.ic1.at(side)) + (filter.a3 * v3);
    filter.ic1.at(side) = (2.0 * v1) - filter.ic1.at(side);
    filter.ic2.at(side) = (2.0 * v2) - filter.ic2.at(side);
    return v2;
}

double triangle(double phase) { return (4.0 * std::abs(phase - 0.5)) - 1.0; }

bool moves(const Lfo& lfo) {
    return lfo.hertz > 0.0 && (lfo.pitchCents != 0.0 || lfo.cutoffCents != 0.0 || lfo.amplitudeDb != 0.0);
}

double semitonesOf(const Zone& zone, int key, const PlaySettings& settings) {
    const double keyed = zone.fixedPitch ? 0.0 : key + zone.keyShift - zone.rootKey;
    return keyed + zone.tuneCents / 100.0 + settings.tuneSemitones;
}

float velocityGain(const Zone& zone, int velocity) {
    if (!zone.velocityDb) {
        return static_cast<float>(velocity) / kVelocityScale;
    }
    return static_cast<float>(std::pow(10.0, zone.velocityDb->at(static_cast<std::size_t>(velocity)) / 20.0));
}

double centsAt(const std::optional<Curve>& curve, int index) {
    return curve ? curve->at(static_cast<std::size_t>(index)) : 0.0;
}

bool loops(const BankZone& zone) { return zone.left->looped() && !zone.zone->oneShot; }

// A key that ignores note off still releases when its wave loops, since it would otherwise play forever.
bool releasesOnNoteOff(const BankZone& zone) { return zone.zone->receivesNoteOff || loops(zone); }

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
        notes_, {}, [](const Note& note) { return note.stage == Stage::release ? note.level : 2.0; });
    if (releasing->stage == Stage::release) {
        return *releasing;
    }
    return *std::ranges::min_element(notes_, {}, &Note::started);
}

void Sampler::noteOn(const Bank& bank, int key, int velocity, int channel, const PlaySettings& settings) {
    const double rate = settings.outputRate;
    for (const BankZone& zone : bank.zones) {
        const Zone& info = *zone.zone;
        if (key < info.keyLo || key > info.keyHi || velocity < info.velocityLo || velocity > info.velocityHi) {
            continue;
        }
        const float gain = settings.volume * info.gain * velocityGain(info, velocity);
        if (info.alternateGroup != 0) {
            chokeGroup(info.alternateGroup, key, channel);
        }
        Note& note = freeNote();
        note = Note{};
        note.stage = Stage::attack;
        note.zone = &zone;
        note.key = key;
        note.channel = channel;
        note.started = ++clock_;
        note.outputRate = rate;
        note.step = info.sampleRate / rate * std::exp2(semitonesOf(info, key, settings) / 12.0);
        note.gainLeft = gain * std::min(1.0F, 1.0F - info.pan);
        note.gainRight = gain * std::min(1.0F, 1.0F + info.pan);
        note.envelope = info.envelope.value_or(
            Envelope{.attackSeconds = settings.attackSeconds, .releaseSeconds = settings.releaseSeconds});
        const Envelope& envelope = note.envelope;
        note.attackStep = envelope.attackGain / framesOf(envelope.attackSeconds, rate);
        note.decay1Ratio = ratioOver(envelope.attackGain, envelope.decay1Gain, envelope.decay1Seconds, rate);
        note.decay2Ratio = ratioOver(envelope.decay1Gain, envelope.sustainGain, envelope.decay2Seconds, rate);
        if (info.lowPass) {
            const LowPass& lowPass = *info.lowPass;
            note.cutoffHertz =
                lowPass.hertz *
                std::exp2((centsAt(lowPass.velocityCents, velocity) + centsAt(lowPass.keyCents, key)) / 1200.0);
            if (lowPass.envelope) {
                note.cutoffCents = lowPass.envelope->initialCents;
                note.cutoffHold = lowPass.envelope->holdSeconds * rate;
            }
            note.filter.onePole = lowPass.onePole;
            tune(note.filter, note.cutoffHertz * std::exp2(note.cutoffCents / 1200.0), lowPass.q, rate);
        }
        note.lfoStep = moves(info.lfo) ? info.lfo.hertz / rate : 0.0;
        note.lfoDelay = info.lfo.delaySeconds * rate;
    }
}

void Sampler::release(Note& note) {
    note.sustained = false;
    note.cutoffSegment = std::max(note.cutoffSegment, kReleaseSegment);
    note.cutoffHold = 0.0;
    if (note.level <= kEnvelopeFloor) {
        note.stage = Stage::idle;
        return;
    }
    note.stage = Stage::release;
    note.releaseRatio = ratioOver(note.level, kEnvelopeFloor, note.envelope.releaseSeconds, note.outputRate);
}

void Sampler::chokeGroup(int group, int key, int channel) {
    for (Note& note : notes_) {
        if (note.stage != Stage::idle && note.channel == channel && note.key != key &&
            note.zone->zone->alternateGroup == group && note.level > kEnvelopeFloor) {
            note.stage = Stage::release;
            note.releaseRatio = ratioOver(note.level, kEnvelopeFloor, kChokeSeconds, note.outputRate);
        }
    }
}

void Sampler::noteOff(int key, int channel) {
    for (Note& note : notes_) {
        if (note.stage != Stage::idle && note.stage != Stage::release && note.key == key && note.channel == channel &&
            releasesOnNoteOff(*note.zone)) {
            if (sustain_.at(static_cast<std::size_t>(channel))) {
                note.sustained = true;
            } else {
                release(note);
            }
        }
    }
}

void Sampler::setSustain(int channel, bool down) {
    sustain_.at(static_cast<std::size_t>(channel)) = down;
    if (down) {
        return;
    }
    for (Note& note : notes_) {
        if (note.sustained && note.channel == channel) {
            release(note);
        }
    }
}

void Sampler::releaseAll() {
    sustain_.fill(false);
    for (Note& note : notes_) {
        if (note.stage != Stage::idle) {
            release(note);
        }
    }
}

void Sampler::silence() {
    sustain_.fill(false);
    for (Note& note : notes_) {
        note.stage = Stage::idle;
    }
}

// Moves the note one frame along its envelope and reports whether it still sounds.
bool Sampler::advanceEnvelope(Note& note) {
    const Envelope& envelope = note.envelope;
    switch (note.stage) {
        case Stage::attack:
            note.level += note.attackStep;
            if (note.level >= envelope.attackGain) {
                note.level = envelope.attackGain;
                note.stage = Stage::decay1;
            }
            break;
        case Stage::decay1:
            if (reached(note.level, note.decay1Ratio, envelope.decay1Gain)) {
                note.stage = Stage::decay2;
            }
            break;
        case Stage::decay2:
            if (reached(note.level, note.decay2Ratio, envelope.sustainGain)) {
                note.stage = Stage::sustain;
            }
            break;
        case Stage::release:
            if (reached(note.level, note.releaseRatio, kEnvelopeFloor)) {
                note.stage = Stage::idle;
            }
            break;
        default:
            break;
    }
    if (note.stage == Stage::sustain && note.level <= kEnvelopeFloor) {
        note.stage = Stage::idle;
    }
    return note.stage != Stage::idle;
}

// Moves the note one frame along its cutoff envelope and reports whether the cutoff moved.
bool Sampler::advanceCutoff(Note& note, const CutoffEnvelope& envelope) {
    if (note.cutoffHold > 0.0) {
        note.cutoffHold -= 1.0;
        return false;
    }
    if (note.cutoffSegment >= envelope.segments.size() ||
        (note.cutoffSegment == kReleaseSegment && note.stage != Stage::release)) {
        return false;
    }
    const CutoffSegment& segment = envelope.segments.at(note.cutoffSegment);
    const double step = segment.centsPerSecond / note.outputRate;
    const double distance = segment.cents - note.cutoffCents;
    if (std::abs(distance) <= step) {
        note.cutoffCents = segment.cents;
        ++note.cutoffSegment;
    } else {
        note.cutoffCents += std::copysign(step, distance);
    }
    return true;
}

void Sampler::renderNote(Note& note, std::span<float> left, std::span<float> right) {
    const Zone& info = *note.zone->zone;
    const Wave& leftWave = *note.zone->left;
    const Wave& rightWave = note.zone->right != nullptr ? *note.zone->right : leftWave;
    const bool looped = loops(*note.zone);
    const auto loopStart = static_cast<double>(leftWave.loopStart);
    const auto loopEnd = static_cast<double>(leftWave.loopEnd);
    const auto end = static_cast<double>(leftWave.samples.size());
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        if (!advanceEnvelope(note)) {
            return;
        }
        double step = note.step;
        double level = note.level;
        double lfoCents = 0.0;
        bool retune = info.lowPass && info.lowPass->envelope && advanceCutoff(note, *info.lowPass->envelope);
        if (note.lfoDelay > 0.0) {
            note.lfoDelay -= 1.0;
        } else if (note.lfoStep > 0.0) {
            const double wobble = triangle(note.lfoPhase);
            note.lfoPhase = std::fmod(note.lfoPhase + note.lfoStep, 1.0);
            step *= std::exp2(info.lfo.pitchCents * wobble / 1200.0);
            level *= std::pow(10.0, info.lfo.amplitudeDb * (1.0 + wobble) / 40.0);
            lfoCents = info.lfo.cutoffCents * wobble;
            retune = retune || info.lfo.cutoffCents != 0.0;
        }
        if (info.lowPass && retune) {
            tune(note.filter, note.cutoffHertz * std::exp2((note.cutoffCents + lfoCents) / 1200.0), info.lowPass->q,
                 note.outputRate);
        }
        double leftSample = sampleAt(leftWave, note.position);
        double rightSample = sampleAt(rightWave, note.position);
        if (info.lowPass) {
            leftSample = lowPassed(note.filter, 0, leftSample);
            rightSample = lowPassed(note.filter, 1, rightSample);
        }
        left[frame] += static_cast<float>(note.gainLeft * level * leftSample);
        right[frame] += static_cast<float>(note.gainRight * level * rightSample);
        note.position += step;
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
