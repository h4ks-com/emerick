#include "emerick/engine/audio_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>
#include <numeric>
#include <ranges>
#include <utility>

#include "emerick/engine/spectrum.hpp"
#include "meter_detail.hpp"
#include "true_peak.hpp"

namespace emerick::engine {

namespace {

constexpr double kAbsoluteGateLufs = -70;
constexpr double kRelativeGateLu = -10;
constexpr double kLoudnessOffset = -0.691;
constexpr std::size_t kStepsPerBlock = 4;
constexpr double kSilenceLevel = 0.001;
constexpr double kMinSilenceSeconds = 0.5;
constexpr double kBandEdgeLimit = 0.45;

using Channels = std::vector<std::vector<float>>;

// The K-weighting stages of BS.1770 for any sample rate, from their analog prototypes as libebur128 derives them.
std::array<Biquad, 2> kWeighting(double sampleRate) {
    constexpr double kShelfHz = 1681.974450955533;
    constexpr double kShelfGainDb = 3.999843853973347;
    constexpr double kShelfQ = 0.7071752369554196;
    constexpr double kHighPassHz = 38.13547087602444;
    constexpr double kHighPassQ = 0.5003270373238773;
    double k = std::tan(std::numbers::pi * kShelfHz / sampleRate);
    const double vh = std::pow(10.0, kShelfGainDb / 20);
    const double vb = std::pow(vh, 0.4996667741545416);
    double a0 = 1 + (k / kShelfQ) + (k * k);
    const Biquad shelf{.b0 = (vh + (vb * k / kShelfQ) + (k * k)) / a0,
                       .b1 = 2 * ((k * k) - vh) / a0,
                       .b2 = (vh - (vb * k / kShelfQ) + (k * k)) / a0,
                       .a1 = 2 * ((k * k) - 1) / a0,
                       .a2 = (1 - (k / kShelfQ) + (k * k)) / a0};
    k = std::tan(std::numbers::pi * kHighPassHz / sampleRate);
    a0 = 1 + (k / kHighPassQ) + (k * k);
    const Biquad highPass{
        .b0 = 1, .b1 = -2, .b2 = 1, .a1 = 2 * ((k * k) - 1) / a0, .a2 = (1 - (k / kHighPassQ) + (k * k)) / a0};
    return {shelf, highPass};
}

// Two Butterworth stages on each edge give the band 24 dB per octave skirts.
std::array<Biquad, 4> bandFilter(const FrequencyBand& band, double sampleRate) {
    const Biquad low = butterworth(band.low, sampleRate, true);
    const Biquad high = butterworth(std::min(band.high, kBandEdgeLimit * sampleRate), sampleRate, false);
    return {low, low, high, high};
}

// The K-weighted power of every channel summed per 100 ms step, which BS.1770 gates in blocks of four steps.
class LoudnessSteps {
public:
    LoudnessSteps(std::size_t channels, double sampleRate)
        : stepFrames_(stepFramesAt(sampleRate)), filters_(channels, kWeighting(sampleRate)) {}

    void add(std::size_t channel, std::span<const float> samples, std::size_t firstFrame) {
        power_.resize(std::max(power_.size(), ((firstFrame + samples.size()) / stepFrames_) + 1));
        std::array<Biquad, 2>& filter = filters_[channel];
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const double weighted = filter[1].process(filter[0].process(samples[index]));
            power_[(firstFrame + index) / stepFrames_] += weighted * weighted;
        }
    }

    // The integrated loudness of the full steps of the first frames.
    [[nodiscard]] double lufs(std::size_t frames) const {
        const std::size_t steps = frames / stepFrames_;
        if (steps < kStepsPerBlock) {
            return kSilentDb;
        }
        const auto blockFrames = static_cast<double>(kStepsPerBlock * stepFrames_);
        const std::vector<double> blocks =
            std::views::iota(kStepsPerBlock, steps + 1) | std::views::transform([&](std::size_t end) {
                const auto begin = power_.begin() + static_cast<std::ptrdiff_t>(end - kStepsPerBlock);
                return std::accumulate(begin, begin + kStepsPerBlock, 0.0) / blockFrames;
            }) |
            std::ranges::to<std::vector>();
        const auto loudness = [](double power) { return kLoudnessOffset + powerDb(power); };
        const auto gatedMean = [&](double gate) {
            auto passing = blocks | std::views::filter([&](double power) { return loudness(power) > gate; });
            const auto count = static_cast<double>(std::ranges::distance(passing));
            return count == 0 ? 0.0 : std::accumulate(passing.begin(), passing.end(), 0.0) / count;
        };
        const double absoluteMean = gatedMean(kAbsoluteGateLufs);
        if (absoluteMean == 0) {
            return kSilentDb;
        }
        return loudness(gatedMean(std::max(kAbsoluteGateLufs, loudness(absoluteMean) + kRelativeGateLu)));
    }

private:
    std::size_t stepFrames_;
    std::vector<std::array<Biquad, 2>> filters_;
    std::vector<double> power_;
};

// The true peak of the frames from first to last of samples, with silence around them.
double truePeak(std::span<const float> samples, std::size_t first, std::size_t last) {
    static const auto kTaps = interpolationTaps();
    double peak = 0;
    for (std::size_t frame = first; frame < last; ++frame) {
        peak = std::max(peak, static_cast<double>(std::abs(samples[frame])));
        for (std::size_t phase = 1; phase < kOversampling; ++phase) {
            double sum = 0;
            for (std::size_t tap = 0; tap < kTapsPerPhase; ++tap) {
                const std::size_t source = frame + kHalfTaps - tap;
                if (frame + kHalfTaps >= tap && source < samples.size()) {
                    sum += samples[source] * kTaps[phase][tap];
                }
            }
            peak = std::max(peak, std::abs(sum));
        }
    }
    return peak;
}

double meanSquare(const Channels& channels, std::size_t begin, std::size_t end) {
    double sum = 0;
    for (const std::vector<float>& channel : channels) {
        for (std::size_t frame = begin; frame < end; ++frame) {
            sum += static_cast<double>(channel[frame]) * channel[frame];
        }
    }
    return sum / static_cast<double>((end - begin) * channels.size());
}

std::vector<BarLevel> barLevels(const Channels& channels, const AnalysisTiming& timing) {
    std::vector<BarLevel> bars;
    const std::size_t frames = channels.front().size();
    const auto barAt = [&](std::size_t frame) {
        return static_cast<int>(
            std::floor((timing.startSeconds + (static_cast<double>(frame) / timing.sampleRate)) / timing.barSeconds));
    };
    for (std::size_t begin = 0; begin < frames;) {
        const int bar = barAt(begin);
        const auto nextBarFrame = static_cast<std::size_t>(
            std::ceil((((bar + 1) * timing.barSeconds) - timing.startSeconds) * timing.sampleRate));
        const std::size_t end = std::clamp(nextBarFrame, begin + 1, frames);
        bars.push_back({.bar = bar + 1, .rmsDb = powerDb(meanSquare(channels, begin, end))});
        begin = end;
    }
    return bars;
}

double bandDb(const Channels& channels, const FrequencyBand& band, double sampleRate) {
    double sum = 0;
    for (const std::vector<float>& channel : channels) {
        std::array<Biquad, 4> filter = bandFilter(band, sampleRate);
        for (const float sample : channel) {
            double value = sample;
            for (Biquad& stage : filter) {
                value = stage.process(value);
            }
            sum += value * value;
        }
    }
    return powerDb(sum / static_cast<double>(channels.front().size() * channels.size()));
}

std::vector<SilentRegion> silentRegions(const Channels& channels, const AnalysisTiming& timing) {
    std::vector<SilentRegion> regions;
    const std::size_t frames = channels.front().size();
    const auto isLoud = [&](std::size_t frame) {
        return std::ranges::any_of(channels,
                                   [&](const auto& channel) { return std::abs(channel[frame]) >= kSilenceLevel; });
    };
    const auto seconds = [&](std::size_t frame) {
        return timing.startSeconds + (static_cast<double>(frame) / timing.sampleRate);
    };
    std::size_t quietStart = 0;
    for (std::size_t frame = 0; frame <= frames; ++frame) {
        if (frame < frames && !isLoud(frame)) {
            continue;
        }
        if (seconds(frame) - seconds(quietStart) >= kMinSilenceSeconds) {
            regions.push_back({.start = seconds(quietStart), .end = seconds(frame)});
        }
        quietStart = frame + 1;
    }
    return regions;
}

StereoImage stereoImage(const std::vector<float>& left, const std::vector<float>& right) {
    double leftPower = 0;
    double rightPower = 0;
    double product = 0;
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        leftPower += static_cast<double>(left[frame]) * left[frame];
        rightPower += static_cast<double>(right[frame]) * right[frame];
        product += static_cast<double>(left[frame]) * right[frame];
    }
    const double scale = std::sqrt(leftPower * rightPower);
    return {.correlation = scale == 0 ? 0 : product / scale, .balanceDb = powerDb(leftPower) - powerDb(rightPower)};
}

}  // namespace

double activeRmsDb(std::span<const double> stepSquares, std::size_t frames, std::size_t stepFrames,
                   std::size_t channels) {
    std::vector<double> stepDb = std::views::iota(std::size_t{0}, stepSquares.size()) |
                                 std::views::transform([&](std::size_t step) {
                                     const std::size_t length = std::min(stepFrames, frames - (step * stepFrames));
                                     return powerDb(stepSquares[step] / static_cast<double>(length * channels));
                                 }) |
                                 std::ranges::to<std::vector>();
    std::erase_if(stepDb, [](double db) { return db <= kActiveGateDb; });
    return stepDb.empty() ? kSilentDb : median(std::move(stepDb));
}

void setSpectrumTone(ToneBalance& tone, std::span<const double> power, double sampleRate) {
    const double total = std::ranges::fold_left(power, 0.0, std::plus{});
    if (total == 0) {
        return;
    }
    const auto hz = [&](std::size_t bin) { return static_cast<double>(bin) * sampleRate / kToneFrame; };
    for (std::size_t bin = 0; bin < power.size(); ++bin) {
        const auto* const band =
            std::ranges::find_if(kToneBands, [&](const FrequencyBand& entry) { return hz(bin) < entry.high; });
        tone.bandShare.at(static_cast<std::size_t>(band - kToneBands.begin())) += power[bin] / total;
        tone.centroidHz += hz(bin) * power[bin] / total;
    }
}

struct TrackMeter::State {
    State(std::size_t channels, double rate)
        : sampleRate(rate),
          stepFrames(stepFramesAt(rate)),
          loudness(channels, rate),
          tails(channels),
          spectra(channels, SpectrumSum(kToneFrame)) {}

    double sampleRate;
    std::size_t stepFrames;
    LoudnessSteps loudness;
    std::vector<double> stepSquares;
    std::size_t frames = 0;
    float samplePeak = 0;
    double interpolatedPeak = 0;
    // Each channel from a few samples before nextPeakFrame on, since a true peak reads samples on both sides.
    std::vector<std::vector<float>> tails;
    std::size_t tailStart = 0;
    std::size_t nextPeakFrame = 0;
    std::vector<SpectrumSum> spectra;

    void addChannel(std::size_t channel, const std::vector<float>& samples) {
        loudness.add(channel, samples, frames);
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const float sample = samples[index];
            stepSquares[(frames + index) / stepFrames] += static_cast<double>(sample) * sample;
            samplePeak = std::max(samplePeak, std::abs(sample));
        }
        tails[channel].insert(tails[channel].end(), samples.begin(), samples.end());
        spectra[channel].add(samples);
    }

    [[nodiscard]] double tailPeak(std::size_t channel, std::size_t last) const {
        return truePeak(tails[channel], nextPeakFrame - tailStart, last - tailStart);
    }

    // Measures the true peak of every frame whose interpolation window has arrived, and keeps only what later frames
    // still read.
    void advanceTruePeak() {
        if (frames <= kHalfTaps || frames - kHalfTaps <= nextPeakFrame) {
            return;
        }
        const std::size_t last = frames - kHalfTaps;
        for (std::size_t channel = 0; channel < tails.size(); ++channel) {
            interpolatedPeak = std::max(interpolatedPeak, tailPeak(channel, last));
        }
        nextPeakFrame = last;
        const std::size_t keepFrom = nextPeakFrame - std::min(nextPeakFrame, kTapsPerPhase - kHalfTaps - 1);
        for (std::vector<float>& tail : tails) {
            tail.erase(tail.begin(), tail.begin() + static_cast<std::ptrdiff_t>(keepFrom - tailStart));
        }
        tailStart = keepFrom;
    }

    [[nodiscard]] ToneBalance tone() const {
        ToneBalance tone{.activeRmsDb = kSilentDb};
        if (frames == 0) {
            return tone;
        }
        tone.activeRmsDb = activeRmsDb(stepSquares, frames, stepFrames, tails.size());
        std::vector<double> power((kToneFrame / 2) + 1);
        for (const SpectrumSum& spectrum : spectra) {
            std::ranges::transform(power, spectrum.sum(), power.begin(), std::plus{});
        }
        setSpectrumTone(tone, power, sampleRate);
        return tone;
    }
};

TrackMeter::TrackMeter(std::size_t channels, double sampleRate)
    : state_(std::make_unique<State>(channels, sampleRate)) {}

TrackMeter::~TrackMeter() = default;
TrackMeter::TrackMeter(TrackMeter&&) noexcept = default;
TrackMeter& TrackMeter::operator=(TrackMeter&&) noexcept = default;

void TrackMeter::add(std::span<const std::vector<float>> chunk) {
    State& state = *state_;
    const std::size_t end = state.frames + chunk.front().size();
    state.stepSquares.resize((end + state.stepFrames - 1) / state.stepFrames);
    for (std::size_t channel = 0; channel < chunk.size(); ++channel) {
        state.addChannel(channel, chunk[channel]);
    }
    state.frames = end;
    state.advanceTruePeak();
}

TrackLevels TrackMeter::levels() const {
    const State& state = *state_;
    double peak = state.interpolatedPeak;
    for (std::size_t channel = 0; channel < state.tails.size(); ++channel) {
        peak = std::max(peak, state.tailPeak(channel, state.frames));
    }
    return {.integratedLufs = state.loudness.lufs(state.frames),
            .samplePeakDbfs = amplitudeDb(state.samplePeak),
            .truePeakDbtp = amplitudeDb(peak),
            .tone = state.tone()};
}

struct LoudnessMeter::State {
    LoudnessSteps loudness;
    std::size_t frames = 0;
};

LoudnessMeter::LoudnessMeter(std::size_t channels, double sampleRate)
    : state_(std::make_unique<State>(LoudnessSteps(channels, sampleRate))) {}

LoudnessMeter::~LoudnessMeter() = default;
LoudnessMeter::LoudnessMeter(LoudnessMeter&&) noexcept = default;
LoudnessMeter& LoudnessMeter::operator=(LoudnessMeter&&) noexcept = default;

void LoudnessMeter::add(std::span<const std::vector<float>> chunk) {
    for (std::size_t channel = 0; channel < chunk.size(); ++channel) {
        state_->loudness.add(channel, chunk[channel], state_->frames);
    }
    state_->frames += chunk.front().size();
}

double LoudnessMeter::integratedLufs() const { return state_->loudness.lufs(state_->frames); }

InterpolationTaps interpolationTaps() {
    InterpolationTaps taps{};
    for (std::size_t phase = 0; phase < kOversampling; ++phase) {
        for (std::size_t tap = 0; tap < kTapsPerPhase; ++tap) {
            const double t = static_cast<double>(tap) - kHalfTaps +
                             (static_cast<double>(phase) / static_cast<double>(kOversampling));
            const double sinc = t == 0 ? 1 : std::sin(std::numbers::pi * t) / (std::numbers::pi * t);
            const double window = 0.5 * (1 + std::cos(std::numbers::pi * t / kHalfTaps));
            taps[phase][tap] = sinc * window;
        }
    }
    return taps;
}

namespace {

TrackLevels wholeLevels(const Channels& channels, double sampleRate) {
    TrackMeter meter(channels.size(), sampleRate);
    if (!channels.empty()) {
        meter.add(channels);
    }
    return meter.levels();
}

}  // namespace

double integratedLufs(const Channels& channels, double sampleRate) {
    return wholeLevels(channels, sampleRate).integratedLufs;
}

double truePeakDbtp(const Channels& channels) {
    // The true peak depends on no sample rate, so any rate the meter takes will do.
    constexpr double kAnyRate = 48000;
    return wholeLevels(channels, kAnyRate).truePeakDbtp;
}

ToneBalance analyzeTone(const Channels& channels, double sampleRate) { return wholeLevels(channels, sampleRate).tone; }

AudioAnalysis analyzeAudio(const Channels& channels, const AnalysisTiming& timing) {
    AudioAnalysis analysis;
    analysis.levels = wholeLevels(channels, timing.sampleRate);
    if (channels.empty() || channels.front().empty()) {
        analysis.bandDb.fill(kSilentDb);
        return analysis;
    }
    for (const std::vector<float>& channel : channels) {
        analysis.clippedSamples +=
            std::ranges::count_if(channel, [](float sample) { return std::abs(sample) >= kClipLevel; });
    }
    analysis.bars = barLevels(channels, timing);
    std::ranges::transform(kFrequencyBands, analysis.bandDb.begin(),
                           [&](const FrequencyBand& band) { return bandDb(channels, band, timing.sampleRate); });
    analysis.silence = silentRegions(channels, timing);
    if (channels.size() == 2) {
        analysis.stereo = stereoImage(channels[0], channels[1]);
    }
    return analysis;
}

}  // namespace emerick::engine
