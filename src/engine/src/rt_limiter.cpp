// The constructor runs on the UI thread while it builds a snapshot and sizes every buffer. Everything else here runs on
// the audio thread and works only on that memory.

#include "rt_limiter.hpp"

#include <algorithm>
#include <cmath>

#include "rt_effects.hpp"

namespace emerick::engine {

namespace {

constexpr double kLookaheadSeconds = 0.0015;
// We aim a hair under the ceiling so float rounding never lands a peak just over it.
constexpr float kCeilingMargin = 0.9999F;

float decibelsToGain(float db) { return std::pow(10.0F, db / 20.0F); }

float param(const EffectParams& params, LimiterParam index) { return params.at(static_cast<std::size_t>(index)); }

}  // namespace

Limiter::Limiter(std::size_t channels, double sampleRate)
    : taps_(interpolationTaps()),
      window_(std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(kLookaheadSeconds * sampleRate)))),
      recent_(channels, std::vector<float>(kTapsPerPhase)),
      delayed_(channels, std::vector<float>(latency())),
      lastSegmentPeak_(channels),
      held_(window_ + 1),
      averaged_(window_, 1.0F),
      averagedSum_(static_cast<double>(window_)) {}

void Limiter::update(const EffectParams& params, const EffectContext& context) {
    ceiling_ = decibelsToGain(param(params, LimiterParam::ceiling)) * kCeilingMargin;
    inputGain_ = decibelsToGain(param(params, LimiterParam::gain));
    release_ = static_cast<float>(
        std::exp(-1000.0 / (std::max(param(params, LimiterParam::release), 0.01F) * context.sampleRate)));
}

// Takes the newest sample and returns the gain that keeps the peak around the sample kHalfTaps frames older under the
// ceiling, from that sample and the interpolated points on either side of it.
float Limiter::neededGain(std::size_t channel, float sample) {
    std::vector<float>& recent = recent_[channel];
    recent[recentAt_] = sample;
    const auto older = [&](std::size_t frames) { return recent[(recentAt_ + kTapsPerPhase - frames) % kTapsPerPhase]; };
    double segmentPeak = std::abs(older(kHalfTaps));
    for (std::size_t phase = 1; phase < kOversampling; ++phase) {
        double sum = 0.0;
        for (std::size_t tap = 0; tap < kTapsPerPhase; ++tap) {
            sum += older(tap) * taps_[phase][tap];
        }
        segmentPeak = std::max(segmentPeak, std::abs(sum));
    }
    const auto peak = static_cast<float>(std::max(segmentPeak, static_cast<double>(lastSegmentPeak_[channel])));
    lastSegmentPeak_[channel] = static_cast<float>(segmentPeak);
    return peak > ceiling_ ? ceiling_ / peak : 1.0F;
}

// The smallest gain of the last window_ + 1 frames, kept in a queue of rising gains.
float Limiter::heldGain(float gain) {
    const std::size_t capacity = held_.size();
    if (heldCount_ > 0 && held_[heldFront_].frame + capacity <= frame_) {
        heldFront_ = (heldFront_ + 1) % capacity;
        --heldCount_;
    }
    while (heldCount_ > 0 && held_[(heldFront_ + heldCount_ - 1) % capacity].gain >= gain) {
        --heldCount_;
    }
    held_[(heldFront_ + heldCount_) % capacity] = {.frame = frame_, .gain = gain};
    ++heldCount_;
    ++frame_;
    return held_[heldFront_].gain;
}

float Limiter::averagedGain(float gain) {
    averagedSum_ += static_cast<double>(gain) - averaged_[averagedAt_];
    averaged_[averagedAt_] = gain;
    averagedAt_ = (averagedAt_ + 1) % averaged_.size();
    return std::min(1.0F, static_cast<float>(averagedSum_ / static_cast<double>(averaged_.size())));
}

void Limiter::process(std::span<float* const> channels, core::Frames offset, core::Frames frames) {
    float deepest = 1.0F;
    for (core::Frames frame = offset; frame < offset + frames; ++frame) {
        float needed = 1.0F;
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            needed = std::min(needed, neededGain(channel, channels[channel][frame] * inputGain_));
        }
        recentAt_ = (recentAt_ + 1) % kTapsPerPhase;
        const float held = heldGain(needed);
        released_ = held < released_ ? held : held + (release_ * (released_ - held));
        const float gain = averagedGain(released_);
        deepest = std::min(deepest, gain);
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            float& delayed = delayed_[channel][delayedAt_];
            float& sample = channels[channel][frame];
            const float input = sample * inputGain_;
            sample = std::clamp(delayed * gain, -ceiling_, ceiling_);
            delayed = input;
        }
        delayedAt_ = (delayedAt_ + 1) % delayed_.front().size();
    }
    reductionDb_ = 20.0F * std::log10(deepest);
}

}  // namespace emerick::engine
