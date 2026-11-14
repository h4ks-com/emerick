#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "emerick/core/time.hpp"
#include "emerick/engine/effects.hpp"
#include "rt_biquad.hpp"

namespace emerick::engine {

struct EffectContext;

inline constexpr std::size_t kAntiAliasFilters = 4;
inline constexpr std::size_t kCabinetFilters = 6;

// The tone stack's circuit after the bilinear transform, a third order filter in transposed direct form.
struct ToneStack {
    std::array<double, 4> b{1.0, 0.0, 0.0, 0.0};
    std::array<double, 3> a{};
};

// A guitar amp: a tube preamp and a sagging power amp run 4x oversampled around a passive tone stack, then the
// presence shelf and a cabinet's EQ curve. Every channel plays through its own copy with the same settings.
class Amp {
public:
    explicit Amp(std::size_t channels);
    void update(const EffectParams& params, const EffectContext& context);
    void process(std::span<float* const> channels, core::Frames offset, core::Frames frames);

private:
    struct ChannelState {
        std::array<BiquadState, kAntiAliasFilters> up{};
        std::array<BiquadState, kAntiAliasFilters> down{};
        double couplingInput = 0.0;
        double couplingOutput = 0.0;
        double gridShift = 0.0;
        std::array<double, 3> tone{};
        double sag = 0.0;
        BiquadState presence{};
        std::array<BiquadState, kCabinetFilters> cabinet{};
    };

    double oversampled(ChannelState& state, double input) const;

    std::vector<ChannelState> states_;
    std::array<Biquad, kAntiAliasFilters> antiAlias_{};
    ToneStack toneStack_;
    Biquad presence_;
    std::array<Biquad, kCabinetFilters> cabinet_{};
    double drive_ = 1.0;
    double bias_ = 0.0;
    double couplingPole_ = 0.0;
    double master_ = 1.0;
    double sagDepth_ = 0.0;
    double sagAttack_ = 0.0;
    double sagRelease_ = 0.0;
    double gridLeak_ = 0.0;
    double gridCharge_ = 0.0;
    float output_ = 1.0F;
};

}  // namespace emerick::engine
