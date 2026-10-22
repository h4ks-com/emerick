// The constructors run on the UI thread while it builds a snapshot and size every buffer. Everything else here runs on
// the audio thread and works only on that memory.

#include "rt_effects.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <ranges>

#include "emerick/core/overloaded.hpp"

namespace emerick::engine {

namespace {

// NOLINTBEGIN(readability-magic-numbers): DSP constants are named where they are used.
constexpr Frames kGlideChunk = 32;
constexpr double kGlideSeconds = 0.02;
constexpr double kDelayGlideSeconds = 0.08;
constexpr double kButterworthQ = std::numbers::sqrt2 / 2.0;
constexpr double kLowCutRest = 5.0;
constexpr double kHighCutRest = 0.49;
constexpr float kSilenceDb = -160.0F;

enum class FilterShape : std::uint8_t { lowPass, highPass, lowShelf, highShelf, peak };

Biquad design(FilterShape shape, double frequency, double gainDb, double q, double sampleRate) {
    const double w0 = 2.0 * std::numbers::pi * std::clamp(frequency, 1.0, sampleRate * kHighCutRest) / sampleRate;
    const double cosine = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * std::max(q, 0.05));
    const double a = std::pow(10.0, gainDb / 40.0);
    const double shelf = 2.0 * std::sqrt(a) * alpha;
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a0 = 1.0;
    double a1 = 0.0;
    double a2 = 0.0;
    switch (shape) {
        case FilterShape::lowPass:
            b0 = b2 = (1.0 - cosine) / 2.0;
            b1 = 1.0 - cosine;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosine;
            a2 = 1.0 - alpha;
            break;
        case FilterShape::highPass:
            b0 = b2 = (1.0 + cosine) / 2.0;
            b1 = -(1.0 + cosine);
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosine;
            a2 = 1.0 - alpha;
            break;
        case FilterShape::peak:
            b0 = 1.0 + (alpha * a);
            b1 = -2.0 * cosine;
            b2 = 1.0 - (alpha * a);
            a0 = 1.0 + (alpha / a);
            a1 = -2.0 * cosine;
            a2 = 1.0 - (alpha / a);
            break;
        case FilterShape::lowShelf:
            b0 = a * ((a + 1.0) - ((a - 1.0) * cosine) + shelf);
            b1 = 2.0 * a * ((a - 1.0) - ((a + 1.0) * cosine));
            b2 = a * ((a + 1.0) - ((a - 1.0) * cosine) - shelf);
            a0 = (a + 1.0) + ((a - 1.0) * cosine) + shelf;
            a1 = -2.0 * ((a - 1.0) + ((a + 1.0) * cosine));
            a2 = (a + 1.0) + ((a - 1.0) * cosine) - shelf;
            break;
        case FilterShape::highShelf:
            b0 = a * ((a + 1.0) + ((a - 1.0) * cosine) + shelf);
            b1 = -2.0 * a * ((a - 1.0) + ((a + 1.0) * cosine));
            b2 = a * ((a + 1.0) + ((a - 1.0) * cosine) - shelf);
            a0 = (a + 1.0) - ((a - 1.0) * cosine) + shelf;
            a1 = 2.0 * ((a - 1.0) - ((a + 1.0) * cosine));
            a2 = (a + 1.0) - ((a - 1.0) * cosine) - shelf;
            break;
    }
    return {.b0 = b0 / a0, .b1 = b1 / a0, .b2 = b2 / a0, .a1 = a1 / a0, .a2 = a2 / a0};
}

double magnitudeDb(const Biquad& filter, double frequency, double sampleRate) {
    const std::complex<double> z1 = std::polar(1.0, -2.0 * std::numbers::pi * frequency / sampleRate);
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> response =
        (filter.b0 + (filter.b1 * z1) + (filter.b2 * z2)) / (1.0 + (filter.a1 * z1) + (filter.a2 * z2));
    return 20.0 * std::log10(std::max(std::abs(response), 1e-12));
}

float bandParam(const EffectParams& params, EqBand band, EqBandParam param) {
    return params.at((static_cast<std::size_t>(band) * kEqBandParams) + static_cast<std::size_t>(param));
}

float param(const EffectParams& params, auto index) { return params.at(static_cast<std::size_t>(index)); }

float dbToGain(float db) { return std::pow(10.0F, db / 20.0F); }

float gainToDb(float gain) { return gain > 0.0F ? 20.0F * std::log10(gain) : kSilenceDb; }

// A coefficient that moves a value most of the way to its target over seconds, applied once per chunk of frames.
double glideCoefficient(double seconds, Frames frames, double sampleRate) {
    return 1.0 - std::exp(-static_cast<double>(frames) / (seconds * sampleRate));
}

float timeCoefficient(float milliseconds, double sampleRate) {
    return static_cast<float>(std::exp(-1000.0 / (std::max(milliseconds, 0.01F) * sampleRate)));
}

// NOLINTEND(readability-magic-numbers)

}  // namespace

EqFilters eqFilters(const EffectParams& params, double sampleRate) {
    EqFilters result;
    const auto addCut = [&](EqBand band, FilterShape shape, std::size_t first, double rest) {
        const double on = std::clamp(bandParam(params, band, EqBandParam::on), 0.0F, 1.0F);
        if (on <= 0.0) {
            return;
        }
        const double frequency =
            std::exp(std::lerp(std::log(rest), std::log(bandParam(params, band, EqBandParam::frequency)), on));
        const auto filters =
            static_cast<std::size_t>(std::clamp(std::lround(bandParam(params, band, EqBandParam::gain)), 0L, 3L)) + 1;
        for (std::size_t index = 0; index < filters; ++index) {
            const double q = index == 0 ? bandParam(params, band, EqBandParam::q) : kButterworthQ;
            result.filters.at(first + index) = design(shape, frequency, 0.0, q, sampleRate);
            result.active.at(first + index) = true;
        }
    };
    const auto addBand = [&](EqBand band, FilterShape shape, std::size_t slot) {
        const float on = std::clamp(bandParam(params, band, EqBandParam::on), 0.0F, 1.0F);
        const float gain = bandParam(params, band, EqBandParam::gain) * on;
        if (gain == 0.0F) {
            return;
        }
        result.filters.at(slot) = design(shape, bandParam(params, band, EqBandParam::frequency), gain,
                                         bandParam(params, band, EqBandParam::q), sampleRate);
        result.active.at(slot) = true;
    };
    addCut(EqBand::lowCut, FilterShape::highPass, 0, kLowCutRest);
    addBand(EqBand::lowShelf, FilterShape::lowShelf, kCutFilters);
    addBand(EqBand::peak1, FilterShape::peak, kCutFilters + 1);
    addBand(EqBand::peak2, FilterShape::peak, kCutFilters + 2);
    addBand(EqBand::peak3, FilterShape::peak, kCutFilters + 3);
    addBand(EqBand::highShelf, FilterShape::highShelf, kCutFilters + 4);
    addCut(EqBand::highCut, FilterShape::lowPass, kCutFilters + 5, sampleRate * kHighCutRest);
    return result;
}

double eqResponseDb(const EffectParams& params, double frequency, double sampleRate) {
    const EqFilters filters = eqFilters(params, sampleRate);
    double total = 0.0;
    for (std::size_t index = 0; index < kEqFilters; ++index) {
        if (filters.active.at(index)) {
            total += magnitudeDb(filters.filters.at(index), frequency, sampleRate);
        }
    }
    return total;
}

Equalizer::Equalizer(std::size_t channels) : states_(channels) {}

void Equalizer::update(const EffectParams& params, const EffectContext& context) {
    filters_ = eqFilters(params, context.sampleRate);
    for (auto& states : states_) {
        for (std::size_t index = 0; index < kEqFilters; ++index) {
            if (!filters_.active.at(index)) {
                states.at(index) = {};
            }
        }
    }
}

void Equalizer::process(std::span<float* const> channels, Frames offset, Frames frames) {
    for (std::size_t channel = 0; channel < channels.size(); ++channel) {
        auto& states = states_[channel];
        for (std::size_t index = 0; index < kEqFilters; ++index) {
            if (!filters_.active.at(index)) {
                continue;
            }
            const Biquad& filter = filters_.filters.at(index);
            BiquadState& state = states.at(index);
            float* samples = channels[channel] + offset;
            for (Frames frame = 0; frame < frames; ++frame) {
                const double input = samples[frame];
                const double output = (filter.b0 * input) + state.z1;
                state.z1 = (filter.b1 * input) - (filter.a1 * output) + state.z2;
                state.z2 = (filter.b2 * input) - (filter.a2 * output);
                samples[frame] = static_cast<float>(output);
            }
        }
    }
}

float compressedDb(float levelDb, float threshold, float ratio, float knee) {
    const float over = levelDb - threshold;
    if (2.0F * over < -knee) {
        return levelDb;
    }
    if (knee > 0.0F && 2.0F * std::abs(over) <= knee) {
        const float into = over + (knee / 2.0F);
        return levelDb + ((1.0F / ratio - 1.0F) * into * into / (2.0F * knee));
    }
    return threshold + (over / ratio);
}

void Compressor::update(const EffectParams& params, const EffectContext& context) {
    threshold_ = param(params, CompressorParam::threshold);
    ratio_ = std::max(param(params, CompressorParam::ratio), 1.0F);
    knee_ = param(params, CompressorParam::knee);
    const float automatic = -compressedDb(0.0F, threshold_, ratio_, knee_) / 2.0F;
    makeupDb_ = param(params, CompressorParam::makeup) + (automatic * param(params, CompressorParam::autoMakeup));
    attack_ = timeCoefficient(param(params, CompressorParam::attack), context.sampleRate);
    release_ = timeCoefficient(param(params, CompressorParam::release), context.sampleRate);
}

void Compressor::process(std::span<float* const> channels, Frames offset, Frames frames) {
    float deepest = 0.0F;
    for (Frames frame = offset; frame < offset + frames; ++frame) {
        float peak = 0.0F;
        for (float* channel : channels) {
            peak = std::max(peak, std::abs(channel[frame]));
        }
        const float levelDb = gainToDb(peak);
        const float target = compressedDb(levelDb, threshold_, ratio_, knee_) - levelDb;
        const float coefficient = target < envelopeDb_ ? attack_ : release_;
        envelopeDb_ = target + (coefficient * (envelopeDb_ - target));
        deepest = std::min(deepest, envelopeDb_);
        const float gain = dbToGain(envelopeDb_ + makeupDb_);
        for (float* channel : channels) {
            channel[frame] *= gain;
        }
    }
    reductionDb_ = deepest;
}

Delay::Delay(std::size_t channels, double sampleRate)
    : lines_(channels, std::vector<float>(static_cast<std::size_t>(std::ceil(kMaxDelayMs / 1000.0 * sampleRate)) + 2)),
      filtered_(channels),
      glide_(glideCoefficient(kDelayGlideSeconds, kGlideChunk, sampleRate)) {}

void Delay::update(const EffectParams& params, const EffectContext& context) {
    const auto note = static_cast<std::size_t>(
        std::clamp(std::lround(param(params, DelayParam::note)), 0L, static_cast<long>(kNoteBeats.size() - 1)));
    constexpr double kMsPerMinute = 60000.0;
    const double milliseconds = param(params, DelayParam::sync) >= 0.5F
                                    ? kNoteBeats.at(note) * kMsPerMinute / std::max(context.bpm, 1.0)
                                    : param(params, DelayParam::time);
    targetSamples_ = std::clamp(milliseconds, 1.0, static_cast<double>(kMaxDelayMs)) * context.sampleRate / 1000.0;
    if (samples_ < 0.0) {
        samples_ = targetSamples_;
    }
    constexpr float kPercent = 100.0F;
    feedback_ = param(params, DelayParam::feedback) / kPercent;
    mix_ = param(params, DelayParam::mix) / kPercent;
    damping_ =
        static_cast<float>(std::exp(-2.0 * std::numbers::pi * param(params, DelayParam::highCut) / context.sampleRate));
}

void Delay::process(std::span<float* const> channels, Frames offset, Frames frames) {
    const std::size_t capacity = lines_.front().size();
    const double from = samples_;
    samples_ += (targetSamples_ - samples_) * glide_;
    for (Frames frame = 0; frame < frames; ++frame) {
        const double delay = from + ((samples_ - from) * static_cast<double>(frame + 1) / static_cast<double>(frames));
        const double read = static_cast<double>(write_ + capacity) - delay;
        const auto whole = static_cast<std::size_t>(std::floor(read));
        const auto fraction = static_cast<float>(read - std::floor(read));
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            std::vector<float>& line = lines_[channel];
            const float echo = std::lerp(line[whole % capacity], line[(whole + 1) % capacity], fraction);
            filtered_[channel] = echo + (damping_ * (filtered_[channel] - echo));
            float& sample = channels[channel][offset + frame];
            line[write_] = sample + (feedback_ * filtered_[channel]);
            sample = std::lerp(sample, echo, mix_);
        }
        write_ = (write_ + 1) % capacity;
    }
}

namespace {

// NOLINTBEGIN(readability-magic-numbers): Freeverb's tuning, in samples at 44.1 kHz.
constexpr std::array<double, 8> kCombTuning{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<double, 4> kAllpassTuning{556, 441, 341, 225};
constexpr double kStereoSpread = 23;
constexpr double kTuningRate = 44100.0;
constexpr double kMinSize = 0.4;
constexpr double kMaxSize = 1.2;
constexpr float kInputGain = 0.03F;
constexpr float kWetGain = 3.0F;
constexpr float kAllpassFeedback = 0.5F;
constexpr float kMaxDamping = 0.4F;
// NOLINTEND(readability-magic-numbers)

std::size_t samplesFor(double tuning, double scale) { return static_cast<std::size_t>(std::ceil(tuning * scale)) + 1; }

float readLine(const std::vector<float>& buffer, std::size_t write, std::size_t length) {
    return buffer[(write + buffer.size() - length) % buffer.size()];
}

void advance(std::size_t& write, std::size_t capacity) { write = (write + 1) % capacity; }

}  // namespace

Reverb::Reverb(std::size_t channels, double sampleRate)
    : scale_(sampleRate / kTuningRate),
      preDelay_{.buffer = std::vector<float>(samplesFor(kMaxPreDelayMs / 1000.0, sampleRate) + 1)} {
    for (std::size_t side = 0; side < std::min<std::size_t>(channels, 2); ++side) {
        const double spread = kStereoSpread * static_cast<double>(side);
        Side made;
        for (const double tuning : kCombTuning) {
            made.combs.push_back({.buffer = std::vector<float>(samplesFor((tuning * kMaxSize) + spread, scale_) + 1)});
        }
        for (const double tuning : kAllpassTuning) {
            const std::size_t length = samplesFor(tuning + spread, scale_);
            made.allpasses.push_back({.buffer = std::vector<float>(length + 1), .length = length});
        }
        sides_.push_back(std::move(made));
    }
}

void Reverb::update(const EffectParams& params, const EffectContext& context) {
    constexpr double kPercent = 100.0;
    const double size = std::lerp(kMinSize, kMaxSize, param(params, ReverbParam::size) / kPercent);
    const double decay = std::max(static_cast<double>(param(params, ReverbParam::decay)), 0.05);
    for (std::size_t side = 0; side < sides_.size(); ++side) {
        const double spread = kStereoSpread * static_cast<double>(side);
        for (std::size_t index = 0; index < kCombTuning.size(); ++index) {
            Line& comb = sides_[side].combs[index];
            comb.length = std::min(samplesFor((kCombTuning.at(index) * size) + spread, scale_), comb.buffer.size() - 1);
            comb.feedback = static_cast<float>(
                std::pow(10.0, -3.0 * static_cast<double>(comb.length) / (decay * context.sampleRate)));
        }
    }
    damping_ = param(params, ReverbParam::damping) / static_cast<float>(kPercent) * kMaxDamping;
    mix_ = param(params, ReverbParam::mix) / static_cast<float>(kPercent);
    preDelay_.length = std::clamp<std::size_t>(
        static_cast<std::size_t>(param(params, ReverbParam::preDelay) / 1000.0 * context.sampleRate), 0,
        preDelay_.buffer.size() - 1);
}

float Reverb::wetSample(Side& side, float input) const {
    float sum = 0.0F;
    for (Line& comb : side.combs) {
        const float output = readLine(comb.buffer, comb.write, comb.length);
        comb.filter = (output * (1.0F - damping_)) + (comb.filter * damping_);
        comb.buffer[comb.write] = input + (comb.filter * comb.feedback);
        advance(comb.write, comb.buffer.size());
        sum += output;
    }
    for (Line& allpass : side.allpasses) {
        const float delayed = readLine(allpass.buffer, allpass.write, allpass.length);
        allpass.buffer[allpass.write] = sum + (delayed * kAllpassFeedback);
        advance(allpass.write, allpass.buffer.size());
        sum = delayed - sum;
    }
    return sum;
}

void Reverb::process(std::span<float* const> channels, Frames offset, Frames frames) {
    for (Frames frame = offset; frame < offset + frames; ++frame) {
        float input = 0.0F;
        for (float* channel : channels) {
            input += channel[frame];
        }
        preDelay_.buffer[preDelay_.write] = input * kInputGain / static_cast<float>(channels.size());
        const float delayed = readLine(preDelay_.buffer, preDelay_.write, preDelay_.length);
        advance(preDelay_.write, preDelay_.buffer.size());
        std::array<float, 2> wet{};
        for (std::size_t side = 0; side < sides_.size(); ++side) {
            wet.at(side) = wetSample(sides_[side], delayed);
        }
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            float& sample = channels[channel][frame];
            sample = std::lerp(sample, wet.at(std::min(channel, sides_.size() - 1)) * kWetGain, mix_);
        }
    }
}

namespace {

std::variant<Equalizer, Compressor, Delay, Reverb> makeProcessor(EffectType type, std::size_t channels,
                                                                 double sampleRate) {
    switch (type) {
        case EffectType::eq:
            return Equalizer{channels};
        case EffectType::compressor:
            return Compressor{};
        case EffectType::delay:
            return Delay{channels, sampleRate};
        case EffectType::reverb:
            return Reverb{channels, sampleRate};
    }
    return Equalizer{channels};
}

}  // namespace

Effect::Effect(EffectType type, std::size_t channels, double sampleRate, Frames maxFrames)
    : type_(type),
      context_{.sampleRate = sampleRate, .bpm = 120.0},
      processor_(makeProcessor(type, channels, sampleRate)),
      glideStep_(static_cast<float>(glideCoefficient(kGlideSeconds, kGlideChunk, sampleRate))),
      dry_(channels, std::vector<float>(static_cast<std::size_t>(maxFrames))) {}

bool Effect::glide(const EffectParams& target) {
    const EffectSpec& spec = effectSpec(type_);
    bool changed = !primed_;
    for (std::size_t index = 0; index < spec.params.size(); ++index) {
        const EffectParam& described = spec.params[index];
        float& value = current_.at(index);
        const float goal = target.at(index);
        if (!primed_ || described.kind == ParamKind::choice) {
            changed = changed || value != goal;
            value = goal;
            continue;
        }
        if (value == goal) {
            continue;
        }
        constexpr float kSnap = 1e-4F;
        value += (goal - value) * glideStep_;
        if (std::abs(goal - value) <= kSnap * (described.maximum - described.minimum)) {
            value = goal;
        }
        changed = true;
    }
    primed_ = true;
    return changed;
}

void Effect::process(std::span<float* const> channels, Frames frames, const EffectParams& target, bool bypass,
                     double bpm) {
    const float wetTarget = bypass ? 0.0F : 1.0F;
    if (!primed_) {
        wet_ = wetTarget;
    }
    if (wet_ == 0.0F && wetTarget == 0.0F) {
        return;
    }
    const bool fading = wet_ != wetTarget;
    if (fading) {
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            std::copy_n(channels[channel], frames, dry_[channel].data());
        }
    }
    bool tempoChanged = context_.bpm != bpm;
    context_.bpm = bpm;
    for (Frames done = 0; done < frames; done += kGlideChunk) {
        const Frames chunk = std::min(kGlideChunk, frames - done);
        if (glide(target) || tempoChanged) {
            std::visit([&](auto& processor) { processor.update(current_, context_); }, processor_);
            tempoChanged = false;
        }
        std::visit([&](auto& processor) { processor.process(channels, done, chunk); }, processor_);
    }
    if (fading) {
        const float step = (wetTarget - wet_) / static_cast<float>(frames);
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            for (Frames frame = 0; frame < frames; ++frame) {
                const float wet = wet_ + (step * static_cast<float>(frame + 1));
                channels[channel][frame] =
                    std::lerp(dry_[channel][static_cast<std::size_t>(frame)], channels[channel][frame], wet);
            }
        }
        wet_ = wetTarget;
    }
    std::visit(core::Overloaded{[&](const Compressor& compressor) {
                                    if (compressor.reductionDb() < reductionDb_.load(std::memory_order_relaxed)) {
                                        reductionDb_.store(compressor.reductionDb(), std::memory_order_relaxed);
                                    }
                                },
                                [](const auto&) {}},
               processor_);
}

}  // namespace emerick::engine
