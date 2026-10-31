// The constructors run on the UI thread while it builds a snapshot and size every buffer. Everything else here runs on
// the audio thread and works only on that memory.

#include "rt_standard_effects.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "rt_effects.hpp"

namespace emerick::engine {

namespace {

// NOLINTBEGIN(readability-magic-numbers): DSP constants are named where they are used.
constexpr float kPercent = 100.0F;
constexpr auto kFilterQ = static_cast<float>(kButterworthQ);
constexpr float kMaxResonanceQ = 20.0F;
constexpr double kNyquistLimit = 0.49;
constexpr float kGateDetectorMs = 10.0F;
constexpr double kDcBlockHz = 10.0;
constexpr float kTubeBias = 0.3F;

template <typename Choice>
Choice choiceParam(const EffectParams& params, auto index, std::size_t count) {
    return static_cast<Choice>(std::clamp(std::lround(param(params, index)), 0L, static_cast<long>(count) - 1));
}

// NOLINTEND(readability-magic-numbers)

}  // namespace

void Gate::update(const EffectParams& params, const EffectContext& context) {
    threshold_ = dbToGain(param(params, GateParam::threshold));
    closedGain_ = dbToGain(-param(params, GateParam::range));
    attack_ = timeCoefficient(param(params, GateParam::attack), context.sampleRate);
    release_ = timeCoefficient(param(params, GateParam::release), context.sampleRate);
    detectorDecay_ = timeCoefficient(kGateDetectorMs, context.sampleRate);
    holdFrames_ = std::lround(param(params, GateParam::hold) / 1000.0 * context.sampleRate);
}

void Gate::process(std::span<float* const> channels, Frames offset, Frames frames) {
    for (Frames frame = offset; frame < offset + frames; ++frame) {
        detector_ = std::max(peakOf(channels, frame), detector_ * detectorDecay_);
        if (detector_ >= threshold_) {
            holding_ = holdFrames_ + 1;
        }
        holding_ = std::max<Frames>(holding_ - 1, 0);
        const float target = detector_ >= threshold_ || holding_ > 0 ? 1.0F : closedGain_;
        const float coefficient = target > gain_ ? attack_ : release_;
        gain_ = target + (coefficient * (gain_ - target));
        for (float* channel : channels) {
            channel[frame] *= gain_;
        }
    }
}

Filter::Filter(std::size_t channels) : states_(channels) {}

void Filter::update(const EffectParams& params, const EffectContext& context) {
    mode_ = choiceParam<FilterMode>(params, FilterParam::mode, kFilterModes.size());
    steep_ = param(params, FilterParam::slope) >= 0.5F;
    const double cutoff =
        std::clamp(static_cast<double>(param(params, FilterParam::cutoff)), 1.0, context.sampleRate * kNyquistLimit);
    targetG_ = static_cast<float>(std::tan(std::numbers::pi * cutoff / context.sampleRate));
    const float resonance = std::clamp(param(params, FilterParam::resonance) / kPercent, 0.0F, 1.0F);
    targetK_ = 1.0F / (kFilterQ * std::pow(kMaxResonanceQ / kFilterQ, resonance));
    if (!primed_) {
        g_ = targetG_;
        k_ = targetK_;
        primed_ = true;
    }
}

void Filter::process(std::span<float* const> channels, Frames offset, Frames frames) {
    const float gStep = (targetG_ - g_) / static_cast<float>(frames);
    const float kStep = (targetK_ - k_) / static_cast<float>(frames);
    const float startG = g_;
    const float startK = k_;
    const auto stage = [this](StateVariableFilter& state, float input, float g, float k) {
        const float a1 = 1.0F / (1.0F + (g * (g + k)));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = input - state.low;
        const float band = (a1 * state.band) + (a2 * v3);
        const float low = state.low + (a2 * state.band) + (a3 * v3);
        state.band = (2.0F * band) - state.band;
        state.low = (2.0F * low) - state.low;
        switch (mode_) {
            case FilterMode::highPass:
                return input - (k * band) - low;
            case FilterMode::bandPass:
                return k * band;
            case FilterMode::lowPass:
                break;
        }
        return low;
    };
    for (std::size_t channel = 0; channel < channels.size(); ++channel) {
        auto& [first, second] = states_[channel];
        float* samples = channels[channel] + offset;
        for (Frames frame = 0; frame < frames; ++frame) {
            const auto step = static_cast<float>(frame + 1);
            const float g = startG + (gStep * step);
            const float k = startK + (kStep * step);
            const float once = stage(first, samples[frame], g, k);
            const float twice = stage(second, once, g, 1.0F / kFilterQ);
            samples[frame] = steep_ ? twice : once;
        }
    }
    g_ = targetG_;
    k_ = targetK_;
}

Saturation::Saturation(std::size_t channels) : blockers_(channels) {}

void Saturation::update(const EffectParams& params, const EffectContext& context) {
    character_ = choiceParam<SaturationCharacter>(params, SaturationParam::character, kSaturationCharacters.size());
    drive_ = dbToGain(param(params, SaturationParam::drive));
    output_ = dbToGain(param(params, SaturationParam::output));
    mix_ = param(params, SaturationParam::mix) / kPercent;
    dcPole_ = static_cast<float>(std::exp(-2.0 * std::numbers::pi * kDcBlockHz / context.sampleRate));
}

void Saturation::process(std::span<float* const> channels, Frames offset, Frames frames) {
    const auto shape = [this](float input) {
        switch (character_) {
            case SaturationCharacter::tape:
                return std::atan(input * std::numbers::pi_v<float> / 2.0F) * 2.0F / std::numbers::pi_v<float>;
            case SaturationCharacter::tube:
                return std::tanh(input + kTubeBias) - std::tanh(kTubeBias);
            case SaturationCharacter::softClip:
                break;
        }
        return std::tanh(input);
    };
    for (std::size_t channel = 0; channel < channels.size(); ++channel) {
        DcBlocker& blocker = blockers_[channel];
        float* samples = channels[channel] + offset;
        for (Frames frame = 0; frame < frames; ++frame) {
            const float shaped = shape(samples[frame] * drive_);
            blocker.output = shaped - blocker.input + (dcPole_ * blocker.output);
            blocker.input = shaped;
            samples[frame] = std::lerp(samples[frame], blocker.output * output_, mix_);
        }
    }
}

Chorus::Chorus(std::size_t channels, double sampleRate)
    : lines_(channels, std::vector<float>(static_cast<std::size_t>(std::ceil((kMaxChorusDelayMs + kMaxChorusDepthMs) /
                                                                             1000.0 * sampleRate)) +
                                          2)) {}

void Chorus::update(const EffectParams& params, const EffectContext& context) {
    phaseStep_ = param(params, ChorusParam::rate) / context.sampleRate;
    delaySamples_ = param(params, ChorusParam::delay) / 1000.0 * context.sampleRate;
    depthSamples_ = param(params, ChorusParam::depth) / kPercent * kMaxChorusDepthMs / 1000.0 * context.sampleRate;
    feedback_ = param(params, ChorusParam::feedback) / kPercent;
    mix_ = param(params, ChorusParam::mix) / kPercent;
}

void Chorus::process(std::span<float* const> channels, Frames offset, Frames frames) {
    constexpr double kQuarterCycle = 0.25;
    const std::size_t capacity = lines_.front().size();
    for (Frames frame = offset; frame < offset + frames; ++frame) {
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            const double sweep =
                0.5 +
                (0.5 * std::sin(2.0 * std::numbers::pi * (phase_ + (kQuarterCycle * static_cast<double>(channel)))));
            const double read = static_cast<double>(write_ + capacity) - delaySamples_ - (depthSamples_ * sweep);
            std::vector<float>& line = lines_[channel];
            const float wet = readBetween(line, read);
            float& sample = channels[channel][frame];
            line[write_] = sample + (feedback_ * wet);
            sample = std::lerp(sample, wet, mix_);
        }
        write_ = (write_ + 1) % capacity;
        phase_ += phaseStep_;
        phase_ -= std::floor(phase_);
    }
}

void Utility::update(const EffectParams& params, const EffectContext& /*context*/) {
    const float width = param(params, UtilityParam::width) / kPercent * (1.0F - param(params, UtilityParam::mono));
    const float balance = param(params, UtilityParam::balance) / kPercent;
    const float gain = dbToGain(param(params, UtilityParam::gain));
    const float left =
        gain * (1.0F - (2.0F * param(params, UtilityParam::invertLeft))) * std::min(1.0F, 1.0F - balance);
    const float right =
        gain * (1.0F - (2.0F * param(params, UtilityParam::invertRight))) * std::min(1.0F, 1.0F + balance);
    const float same = (1.0F + width) / 2.0F;
    const float other = (1.0F - width) / 2.0F;
    target_ = {left * same, left * other, right * other, right * same};
    if (!primed_) {
        matrix_ = target_;
        primed_ = true;
    }
}

void Utility::process(std::span<float* const> channels, Frames offset, Frames frames) {
    std::array<float, 4> step{};
    for (std::size_t index = 0; index < step.size(); ++index) {
        step.at(index) = (target_.at(index) - matrix_.at(index)) / static_cast<float>(frames);
    }
    for (Frames frame = offset; frame < offset + frames; ++frame) {
        for (std::size_t index = 0; index < step.size(); ++index) {
            matrix_.at(index) += step.at(index);
        }
        const auto [leftToLeft, rightToLeft, leftToRight, rightToRight] = matrix_;
        if (channels.size() < 2) {
            channels[0][frame] *= leftToLeft + rightToLeft;
            continue;
        }
        const float left = channels[0][frame];
        const float right = channels[1][frame];
        channels[0][frame] = (leftToLeft * left) + (rightToLeft * right);
        channels[1][frame] = (leftToRight * left) + (rightToRight * right);
    }
    matrix_ = target_;
}

}  // namespace emerick::engine
