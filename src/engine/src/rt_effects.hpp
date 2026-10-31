#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <span>
#include <variant>
#include <vector>

#include "emerick/engine/effects.hpp"
#include "emerick/engine/session.hpp"
#include "rt_limiter.hpp"

namespace emerick::engine {

struct Biquad {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

struct BiquadState {
    double z1 = 0.0;
    double z2 = 0.0;
};

// Each cut band cascades up to four filters for its slope, so the EQ has a fixed slot per filter and a band turning on
// never moves another band's state.
inline constexpr std::size_t kCutFilters = 4;
inline constexpr std::size_t kEqFilters = (2 * kCutFilters) + 5;

struct EqFilters {
    std::array<Biquad, kEqFilters> filters{};
    std::array<bool, kEqFilters> active{};
};

// A band's on param may sit between 0 and 1 while it fades, which scales a shelf or peak's gain and slides a cut toward
// the edge of the spectrum.
EqFilters eqFilters(const EffectParams& params, double sampleRate);

struct EffectContext {
    double sampleRate = 48000.0;
    double bpm = 120.0;
};

class Equalizer {
public:
    explicit Equalizer(std::size_t channels);
    void update(const EffectParams& params, const EffectContext& context);
    void process(std::span<float* const> channels, Frames offset, Frames frames);

private:
    EqFilters filters_;
    std::vector<std::array<BiquadState, kEqFilters>> states_;
};

class Compressor {
public:
    void update(const EffectParams& params, const EffectContext& context);
    void process(std::span<float* const> channels, Frames offset, Frames frames);
    // The most gain the last block took away, in dB.
    [[nodiscard]] float reductionDb() const { return reductionDb_; }

private:
    float threshold_ = 0.0F;
    float ratio_ = 1.0F;
    float knee_ = 0.0F;
    float makeupDb_ = 0.0F;
    float attack_ = 0.0F;
    float release_ = 0.0F;
    float envelopeDb_ = 0.0F;
    float reductionDb_ = 0.0F;
};

// The static curve: the level a compressor with this threshold, ratio and knee turns levelDb into.
float compressedDb(float levelDb, float threshold, float ratio, float knee);

class Delay {
public:
    Delay(std::size_t channels, double sampleRate);
    void update(const EffectParams& params, const EffectContext& context);
    void process(std::span<float* const> channels, Frames offset, Frames frames);

private:
    std::vector<std::vector<float>> lines_;
    std::vector<float> filtered_;
    std::size_t write_ = 0;
    double glide_ = 0.0;
    double samples_ = -1.0;
    double targetSamples_ = 0.0;
    float feedback_ = 0.0F;
    float mix_ = 0.0F;
    float damping_ = 0.0F;
};

// After Jezar's public domain Freeverb: eight damped combs into four allpasses per side, behind a pre-delay.
class Reverb {
public:
    Reverb(std::size_t channels, double sampleRate);
    void update(const EffectParams& params, const EffectContext& context);
    void process(std::span<float* const> channels, Frames offset, Frames frames);

private:
    struct Line {
        std::vector<float> buffer;
        std::size_t write = 0;
        std::size_t length = 1;
        float feedback = 0.0F;
        float filter = 0.0F;
    };
    struct Side {
        std::vector<Line> combs;
        std::vector<Line> allpasses;
    };

    float wetSample(Side& side, float input) const;

    double scale_;
    std::vector<Side> sides_;
    Line preDelay_;
    float damping_ = 0.0F;
    float mix_ = 0.0F;
};

// One insert's processor with its smoothed params. A snapshot builds it off the audio thread, and later snapshots
// share it while the insert lives, so its tails and smoothing carry across edits.
class Effect {
public:
    Effect(EffectType type, std::size_t channels, double sampleRate, Frames maxFrames);
    // Glides from the params it last played to target, and crossfades to dry when bypass turns on; a bypassed effect
    // leaves the samples untouched.
    void process(std::span<float* const> channels, Frames frames, const EffectParams& target, bool bypass, double bpm);
    // Returns and resets the most gain a compressor took away since the last call. The UI thread reads it while the
    // audio thread lowers it.
    float takeReductionDb() { return reductionDb_.exchange(0.0F, std::memory_order_relaxed); }

private:
    bool glide(const EffectParams& target);

    EffectType type_;
    EffectContext context_;
    std::variant<Equalizer, Compressor, Delay, Reverb, Limiter> processor_;
    EffectParams current_{};
    bool primed_ = false;
    float wet_ = 1.0F;
    float glideStep_ = 1.0F;
    std::vector<std::vector<float>> dry_;
    std::atomic<float> reductionDb_{0.0F};
};

}  // namespace emerick::engine
