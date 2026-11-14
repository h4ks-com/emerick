// The constructor runs on the UI thread while it builds a snapshot and sizes every buffer. Everything else here runs on
// the audio thread and works only on that memory.

#include "rt_amp.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

#include "rt_effects.hpp"

namespace emerick::engine {

namespace {

// NOLINTBEGIN(readability-magic-numbers): the circuit values and voicings are named in these tables.
constexpr std::size_t kAmpOversampling = 4;
constexpr double kAntiAliasCutoff = 0.45;
constexpr double kKnobRange = 10.0;
constexpr double kBassTaper = 3.4;
constexpr double kToneReferenceHz = 1000.0;
constexpr double kPresenceDbPerStep = 1.6;
constexpr double kMasterGainAtTen = 4.0;
constexpr double kSagAttackMs = 5.0;
constexpr double kSagReleaseMs = 150.0;
constexpr double kNegativeHeadroom = 0.7;
constexpr double kGridThreshold = 0.5;
constexpr double kGridRecoveryMs = 30.0;
constexpr double kGridCharge = 1.0;

// The resistors in ohms and capacitors in farads of a treble, mid and bass tone stack.
struct ToneStackParts {
    double r1;
    double r2;
    double r3;
    double r4;
    double c1;
    double c2;
    double c3;
};

struct AmpVoicing {
    ToneStackParts toneStack;
    double minDriveDb;
    double driveRangeDb;
    double bias;
    double couplingHz;
    double sag;
    double presenceHz;
};

// Indexed by AmpModel: a Vox AC30 top boost, a Fender blackface and a Marshall JCM800 tone stack.
constexpr std::array<AmpVoicing, kAmpModels.size()> kVoicings{{
    {.toneStack = {.r1 = 1e6, .r2 = 1e6, .r3 = 10e3, .r4 = 100e3, .c1 = 50e-12, .c2 = 22e-9, .c3 = 22e-9},
     .minDriveDb = -6.0,
     .driveRangeDb = 36.0,
     .bias = 0.2,
     .couplingHz = 30.0,
     .sag = 0.3,
     .presenceHz = 3500.0},
    {.toneStack = {.r1 = 250e3, .r2 = 250e3, .r3 = 10e3, .r4 = 100e3, .c1 = 250e-12, .c2 = 100e-9, .c3 = 47e-9},
     .minDriveDb = -12.0,
     .driveRangeDb = 30.0,
     .bias = 0.1,
     .couplingHz = 20.0,
     .sag = 0.15,
     .presenceHz = 4500.0},
    {.toneStack = {.r1 = 220e3, .r2 = 1e6, .r3 = 22e3, .r4 = 33e3, .c1 = 470e-12, .c2 = 22e-9, .c3 = 22e-9},
     .minDriveDb = 0.0,
     .driveRangeDb = 42.0,
     .bias = 0.3,
     .couplingHz = 60.0,
     .sag = 0.25,
     .presenceHz = 3500.0},
}};

struct CabinetVoicing {
    double resonanceHz;
    double resonanceQ;
    double bodyHz;
    double bodyDb;
    double presenceHz;
    double presenceDb;
    double rollOffHz;
};

// Indexed by AmpCabinet after Off.
constexpr std::array<CabinetVoicing, kAmpCabinets.size() - 1> kCabinets{{
    {.resonanceHz = 110.0,
     .resonanceQ = 1.1,
     .bodyHz = 600.0,
     .bodyDb = 3.0,
     .presenceHz = 2500.0,
     .presenceDb = 3.0,
     .rollOffHz = 5000.0},
    {.resonanceHz = 95.0,
     .resonanceQ = 1.3,
     .bodyHz = 500.0,
     .bodyDb = 1.5,
     .presenceHz = 2800.0,
     .presenceDb = 4.0,
     .rollOffHz = 5500.0},
    {.resonanceHz = 85.0,
     .resonanceQ = 1.8,
     .bodyHz = 400.0,
     .bodyDb = -2.0,
     .presenceHz = 3200.0,
     .presenceDb = 4.0,
     .rollOffHz = 5000.0},
}};

// The circuit's transfer function in s, lowest power first, after Yeh and Smith's analysis of the Fender Bassman tone
// stack; t and m run from 0 to 1 and l is the bass pot's log taper.
struct AnalogToneStack {
    std::array<double, 4> b;
    std::array<double, 4> a;
};

AnalogToneStack analogToneStack(const ToneStackParts& p, double t, double m, double l) {
    const auto [r1, r2, r3, r4, c1, c2, c3] = p;
    const double c123 = c1 * c2 * c3;
    const double b1 = (t * c1 * r1) + (m * c3 * r3) + (l * (c1 * r2 + c2 * r2)) + (c1 * r3 + c2 * r3);
    const double b2 =
        (t * (c1 * c2 * r1 * r4 + c1 * c3 * r1 * r4)) - (m * m * (c1 * c3 * r3 * r3 + c2 * c3 * r3 * r3)) +
        (m * (c1 * c3 * r1 * r3 + c1 * c3 * r3 * r3 + c2 * c3 * r3 * r3)) +
        (l * (c1 * c2 * r1 * r2 + c1 * c2 * r2 * r4 + c1 * c3 * r2 * r4)) +
        (l * m * (c1 * c3 * r2 * r3 + c2 * c3 * r2 * r3)) + (c1 * c2 * r1 * r3 + c1 * c2 * r3 * r4 + c1 * c3 * r3 * r4);
    const double b3 = (l * m * c123 * (r1 * r2 * r3 + r2 * r3 * r4)) - (m * m * c123 * (r1 * r3 * r3 + r3 * r3 * r4)) +
                      (m * c123 * (r1 * r3 * r3 + r3 * r3 * r4)) + (t * c123 * r1 * r3 * r4) -
                      (t * m * c123 * r1 * r3 * r4) + (t * l * c123 * r1 * r2 * r4);
    const double a1 = (c1 * r1 + c1 * r3 + c2 * r3 + c2 * r4 + c3 * r4) + (m * c3 * r3) + (l * (c1 * r2 + c2 * r2));
    const double a2 = (m * (c1 * c3 * r1 * r3 - c2 * c3 * r3 * r4 + c1 * c3 * r3 * r3 + c2 * c3 * r3 * r3)) +
                      (l * m * (c1 * c3 * r2 * r3 + c2 * c3 * r2 * r3)) -
                      (m * m * (c1 * c3 * r3 * r3 + c2 * c3 * r3 * r3)) +
                      (l * (c1 * c2 * r2 * r4 + c1 * c2 * r1 * r2 + c1 * c3 * r2 * r4 + c2 * c3 * r2 * r4)) +
                      (c1 * c2 * r1 * r4 + c1 * c3 * r1 * r4 + c1 * c2 * r3 * r4 + c1 * c2 * r1 * r3 +
                       c1 * c3 * r3 * r4 + c2 * c3 * r3 * r4);
    const double a3 = (l * m * c123 * (r1 * r2 * r3 + r2 * r3 * r4)) - (m * m * c123 * (r1 * r3 * r3 + r3 * r3 * r4)) +
                      (m * c123 * (r3 * r3 * r4 + r1 * r3 * r3 - r1 * r3 * r4)) + (l * c123 * r1 * r2 * r4) +
                      (c123 * r1 * r3 * r4);
    return {.b = {0.0, b1, b2, b3}, .a = {1.0, a1, a2, a3}};
}

double analogGain(const AnalogToneStack& stack, double frequency) {
    const std::complex<double> s{0.0, 2.0 * std::numbers::pi * frequency};
    std::complex<double> numerator;
    std::complex<double> denominator;
    std::complex<double> power{1.0, 0.0};
    for (std::size_t index = 0; index < stack.b.size(); ++index) {
        numerator += stack.b.at(index) * power;
        denominator += stack.a.at(index) * power;
        power *= s;
    }
    return std::abs(numerator / denominator);
}

ToneStack bilinear(const AnalogToneStack& stack, double sampleRate, double gain) {
    const double c = 2.0 * sampleRate;
    const auto [a0, a1, a2, a3] = stack.a;
    const double b1c = stack.b[1] * c;
    const double b2c = stack.b[2] * c * c;
    const double b3c = stack.b[3] * c * c * c;
    const double a1c = a1 * c;
    const double a2c = a2 * c * c;
    const double a3c = a3 * c * c * c;
    const double d0 = a0 + a1c + a2c + a3c;
    const double scale = gain / d0;
    return {.b = {(b1c + b2c + b3c) * scale, (b1c - b2c - (3.0 * b3c)) * scale, (-b1c - b2c + (3.0 * b3c)) * scale,
                  (-b1c + b2c - b3c) * scale},
            .a = {((3.0 * a0) + a1c - a2c - (3.0 * a3c)) / d0, ((3.0 * a0) - a1c - a2c + (3.0 * a3c)) / d0,
                  (a0 - a1c + a2c - a3c) / d0}};
}

double toneSample(const ToneStack& filter, std::array<double, 3>& state, double input) {
    const double output = (filter.b[0] * input) + state[0];
    state[0] = (filter.b[1] * input) - (filter.a[0] * output) + state[1];
    state[1] = (filter.b[2] * input) - (filter.a[1] * output) + state[2];
    state[2] = (filter.b[3] * input) - (filter.a[2] * output);
    return output;
}

// A triode clips its negative swing earlier than its positive one.
double tubeStage(double input) {
    return input >= 0.0 ? std::tanh(input) : kNegativeHeadroom * std::tanh(input / kNegativeHeadroom);
}

double knob(const EffectParams& params, AmpParam index) { return param(params, index) / kKnobRange; }

double bassTaper(double position) { return std::exp((position - 1.0) * kBassTaper); }

std::array<Biquad, kCabinetFilters> cabinetFilters(AmpCabinet cabinet, double sampleRate) {
    if (cabinet == AmpCabinet::off) {
        return {};
    }
    const CabinetVoicing& voicing = kCabinets.at(static_cast<std::size_t>(cabinet) - 1);
    return {design(FilterShape::highPass, voicing.resonanceHz, 0.0, voicing.resonanceQ, sampleRate),
            design(FilterShape::peak, voicing.bodyHz, voicing.bodyDb, 1.0, sampleRate),
            design(FilterShape::peak, voicing.presenceHz, voicing.presenceDb, 1.4, sampleRate),
            design(FilterShape::lowPass, voicing.rollOffHz, 0.0, 0.54, sampleRate),
            design(FilterShape::lowPass, voicing.rollOffHz, 0.0, 1.31, sampleRate),
            design(FilterShape::lowPass, voicing.rollOffHz * 1.5, 0.0, kButterworthQ, sampleRate)};
}

template <typename Choice>
Choice choice(const EffectParams& params, AmpParam index, std::size_t count) {
    return static_cast<Choice>(std::clamp(std::lround(param(params, index)), 0L, static_cast<long>(count) - 1));
}

// NOLINTEND(readability-magic-numbers)

}  // namespace

Amp::Amp(std::size_t channels) : states_(channels) {}

void Amp::update(const EffectParams& params, const EffectContext& context) {
    const double rate = context.sampleRate * kAmpOversampling;
    const AmpVoicing& voicing =
        kVoicings.at(static_cast<std::size_t>(choice<AmpModel>(params, AmpParam::model, kAmpModels.size())));
    for (std::size_t index = 0; index < kAntiAliasFilters; ++index) {
        const double angle = std::numbers::pi * static_cast<double>((2 * index) + 1) / (4.0 * kAntiAliasFilters);
        antiAlias_.at(index) = design(FilterShape::lowPass, kAntiAliasCutoff * context.sampleRate, 0.0,
                                      1.0 / (2.0 * std::cos(angle)), rate);
    }
    const double middle = 0.5;
    const double makeup =
        1.0 / analogGain(analogToneStack(voicing.toneStack, middle, middle, bassTaper(middle)), kToneReferenceHz);
    toneStack_ = bilinear(analogToneStack(voicing.toneStack, knob(params, AmpParam::treble),
                                          knob(params, AmpParam::mids), bassTaper(knob(params, AmpParam::bass))),
                          rate, makeup);
    drive_ = dbToGain(static_cast<float>(voicing.minDriveDb + (knob(params, AmpParam::gain) * voicing.driveRangeDb)));
    bias_ = voicing.bias;
    couplingPole_ = std::exp(-2.0 * std::numbers::pi * voicing.couplingHz / rate);
    const double master = knob(params, AmpParam::master);
    master_ = master * master * kMasterGainAtTen;
    sagDepth_ = voicing.sag;
    sagAttack_ = timeCoefficient(static_cast<float>(kSagAttackMs), rate);
    sagRelease_ = timeCoefficient(static_cast<float>(kSagReleaseMs), rate);
    gridLeak_ = timeCoefficient(static_cast<float>(kGridRecoveryMs), rate);
    gridCharge_ = kGridCharge * (1.0 - gridLeak_);
    presence_ = design(FilterShape::highShelf, voicing.presenceHz,
                       (param(params, AmpParam::presence) - (kKnobRange / 2.0)) * kPresenceDbPerStep, kButterworthQ,
                       context.sampleRate);
    cabinet_ = cabinetFilters(choice<AmpCabinet>(params, AmpParam::cabinet, kAmpCabinets.size()), context.sampleRate);
    output_ = dbToGain(param(params, AmpParam::output));
}

double Amp::oversampled(ChannelState& state, double input) const {
    double output = 0.0;
    for (std::size_t phase = 0; phase < kAmpOversampling; ++phase) {
        double sample = phase == 0 ? input * kAmpOversampling : 0.0;
        for (std::size_t index = 0; index < kAntiAliasFilters; ++index) {
            sample = filterSample(antiAlias_.at(index), state.up.at(index), sample);
        }
        // We let loud peaks shift the bias as grid current does in a real preamp, so even harmonics survive full drive.
        const double grid = (sample * drive_) + state.gridShift;
        const double preamp = tubeStage(grid + bias_) - tubeStage(bias_);
        state.gridShift = gridLeak_ * (state.gridShift - (gridCharge_ * std::max(grid - kGridThreshold, 0.0)));
        state.couplingOutput = couplingPole_ * (state.couplingOutput + preamp - state.couplingInput);
        state.couplingInput = preamp;
        const double toned = toneSample(toneStack_, state.tone, state.couplingOutput);
        const double powered = std::tanh(toned * master_ / (1.0 + (sagDepth_ * state.sag)));
        const double level = std::abs(powered);
        state.sag = level + ((level > state.sag ? sagAttack_ : sagRelease_) * (state.sag - level));
        sample = powered;
        for (std::size_t index = 0; index < kAntiAliasFilters; ++index) {
            sample = filterSample(antiAlias_.at(index), state.down.at(index), sample);
        }
        output = sample;
    }
    return output;
}

void Amp::process(std::span<float* const> channels, core::Frames offset, core::Frames frames) {
    for (std::size_t channel = 0; channel < channels.size(); ++channel) {
        ChannelState& state = states_[channel];
        float* samples = channels[channel] + offset;
        for (core::Frames frame = 0; frame < frames; ++frame) {
            double sample = filterSample(presence_, state.presence, oversampled(state, samples[frame]));
            for (std::size_t index = 0; index < kCabinetFilters; ++index) {
                sample = filterSample(cabinet_.at(index), state.cabinet.at(index), sample);
            }
            samples[frame] = static_cast<float>(sample) * output_;
        }
    }
}

}  // namespace emerick::engine
