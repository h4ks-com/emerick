#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "emerick/core/time.hpp"
#include "emerick/engine/effects.hpp"
#include "true_peak.hpp"

namespace emerick::engine {

struct EffectContext;

// A true peak limiter. It estimates each sample's inter-sample peak with the 4x interpolation the loudness analysis
// uses, holds the gain each peak needs across a lookahead window, releases it slowly, and averages it over the window,
// so the gain is already down when the delayed peak plays.
class Limiter {
public:
    Limiter(std::size_t channels, double sampleRate);
    void update(const EffectParams& params, const EffectContext& context);
    void process(std::span<float* const> channels, core::Frames offset, core::Frames frames);
    // The most gain the last block took away, in dB.
    [[nodiscard]] float reductionDb() const { return reductionDb_; }
    // How many frames late the limiter plays its input.
    [[nodiscard]] std::size_t latency() const { return window_ + kHalfTaps; }

private:
    struct HeldGain {
        std::size_t frame = 0;
        float gain = 1.0F;
    };

    float neededGain(std::size_t channel, float sample);
    float heldGain(float gain);
    float averagedGain(float gain);

    InterpolationTaps taps_;
    std::size_t window_;
    std::vector<std::vector<float>> recent_;
    std::vector<std::vector<float>> delayed_;
    std::vector<float> lastSegmentPeak_;
    std::size_t recentAt_ = 0;
    std::size_t delayedAt_ = 0;
    std::vector<HeldGain> held_;
    std::size_t heldFront_ = 0;
    std::size_t heldCount_ = 0;
    std::size_t frame_ = 0;
    std::vector<float> averaged_;
    std::size_t averagedAt_ = 0;
    double averagedSum_ = 0.0;
    float released_ = 1.0F;
    float ceiling_ = 1.0F;
    float inputGain_ = 1.0F;
    float release_ = 0.0F;
    float reductionDb_ = 0.0F;
};

}  // namespace emerick::engine
