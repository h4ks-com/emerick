#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "emerick/engine/effects.hpp"
#include "emerick/engine/session.hpp"

namespace emerick::engine {

struct EffectContext;

class Gate {
public:
    void update(const EffectParams& params, const EffectContext& context);
    void process(std::span<float* const> channels, Frames offset, Frames frames);

private:
    float threshold_ = 0.0F;
    float closedGain_ = 0.0F;
    float attack_ = 0.0F;
    float release_ = 0.0F;
    float detectorDecay_ = 0.0F;
    Frames holdFrames_ = 0;
    Frames holding_ = 0;
    float detector_ = 0.0F;
    float gain_ = 0.0F;
};

// A topology-preserving state variable filter, so its cutoff and resonance can move every sample without clicks.
struct StateVariableFilter {
    float low = 0.0F;
    float band = 0.0F;
};

class Filter {
public:
    explicit Filter(std::size_t channels);
    void update(const EffectParams& params, const EffectContext& context);
    void process(std::span<float* const> channels, Frames offset, Frames frames);

private:
    std::vector<std::array<StateVariableFilter, 2>> states_;
    FilterMode mode_ = FilterMode::lowPass;
    bool steep_ = false;
    bool primed_ = false;
    float g_ = 0.0F;
    float k_ = 0.0F;
    float targetG_ = 0.0F;
    float targetK_ = 0.0F;
};

class Saturation {
public:
    explicit Saturation(std::size_t channels);
    void update(const EffectParams& params, const EffectContext& context);
    void process(std::span<float* const> channels, Frames offset, Frames frames);

private:
    struct DcBlocker {
        float input = 0.0F;
        float output = 0.0F;
    };

    std::vector<DcBlocker> blockers_;
    SaturationCharacter character_ = SaturationCharacter::softClip;
    float drive_ = 1.0F;
    float output_ = 1.0F;
    float mix_ = 1.0F;
    float dcPole_ = 0.0F;
};

class Chorus {
public:
    Chorus(std::size_t channels, double sampleRate);
    void update(const EffectParams& params, const EffectContext& context);
    void process(std::span<float* const> channels, Frames offset, Frames frames);

private:
    std::vector<std::vector<float>> lines_;
    std::size_t write_ = 0;
    double phase_ = 0.0;
    double phaseStep_ = 0.0;
    double delaySamples_ = 0.0;
    double depthSamples_ = 0.0;
    float feedback_ = 0.0F;
    float mix_ = 0.0F;
};

class Utility {
public:
    void update(const EffectParams& params, const EffectContext& context);
    void process(std::span<float* const> channels, Frames offset, Frames frames);

private:
    std::array<float, 4> matrix_{};
    std::array<float, 4> target_{};
    bool primed_ = false;
};

}  // namespace emerick::engine
