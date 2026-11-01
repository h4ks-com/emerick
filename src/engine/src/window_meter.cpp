#include "emerick/engine/window_meter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <numeric>
#include <ranges>
#include <utility>

#include "emerick/engine/spectrum.hpp"
#include "meter_detail.hpp"

namespace emerick::engine {

namespace {

constexpr double kTonalFloorHz = 1000;
// A sine under a Hann window keeps nearly all its energy within this many bins of its peak.
constexpr std::size_t kNarrowBins = 3;
// We treat a peak below this share of the window's energy as leakage from lower sounds.
constexpr double kMinTonalShare = 0.001;
constexpr double kOnsetHopSeconds = 0.005;
constexpr double kOnsetLoudShare = 0.25;
constexpr double kOnsetQuietShare = 0.05;

struct TonalPeak {
    double hz = 0;
    double tonality = 0;
};

double sumOf(std::span<const double> power, std::size_t first, std::size_t last) {
    return std::accumulate(power.begin() + static_cast<std::ptrdiff_t>(first),
                           power.begin() + static_cast<std::ptrdiff_t>(last) + 1, 0.0);
}

TonalPeak tonalPeak(std::span<const double> power, double sampleRate) {
    const double binHz = sampleRate / kToneFrame;
    const auto first = static_cast<std::size_t>(std::ceil(kTonalFloorHz / binHz));
    if (power.size() < first + (2 * kNarrowBins) + 1) {
        return {};
    }
    const std::span<const double> above = power.subspan(first, power.size() - first - kNarrowBins);
    const std::size_t peak = first + static_cast<std::size_t>(std::ranges::max_element(above) - above.begin());
    const double narrow = sumOf(power, peak - kNarrowBins, peak + kNarrowBins);
    const double total = std::ranges::fold_left(power, 0.0, std::plus{});
    if (total == 0 || narrow < total * kMinTonalShare) {
        return {};
    }
    const double wide = sumOf(power, peak / 2, std::min(power.size() - 1, peak * 2));
    const auto level = [&](std::size_t bin) { return std::log(power[bin] + (total * 1e-12)); };
    const double curve = level(peak - 1) - (2 * level(peak)) + level(peak + 1);
    const double offset = curve == 0 ? 0 : 0.5 * (level(peak - 1) - level(peak + 1)) / curve;
    return {.hz = (static_cast<double>(peak) + offset) * binHz, .tonality = narrow / wide};
}

int onsetsOf(const std::vector<double>& envelope) {
    const double loudest = envelope.empty() ? 0 : std::ranges::max(envelope);
    int onsets = 0;
    bool quiet = true;
    for (const double power : envelope) {
        if (quiet && power > loudest * kOnsetLoudShare) {
            ++onsets;
            quiet = false;
        } else if (power < loudest * kOnsetQuietShare) {
            quiet = true;
        }
    }
    return onsets;
}

struct Window {
    std::size_t frames = 0;
    double squares = 0;
    double sum = 0;
    float peak = 0;
    std::int64_t clipped = 0;
    std::vector<double> stepSquares;
    std::vector<double> envelope;
    SpectrumSum monoSpectrum{kToneFrame};
    std::vector<SpectrumSum> spectra;
};

}  // namespace

struct WindowMeter::State {
    State(std::size_t channelCount, double rate, std::vector<std::size_t> windowEnds)
        : channels(channelCount),
          sampleRate(rate),
          stepFrames(stepFramesAt(rate)),
          hopFrames(static_cast<std::size_t>(std::lround(kOnsetHopSeconds * rate))),
          ends(std::move(windowEnds)),
          highPass{butterworth(kTonalFloorHz, rate, true), butterworth(kTonalFloorHz, rate, true)} {}

    std::size_t channels;
    double sampleRate;
    std::size_t stepFrames;
    std::size_t hopFrames;
    std::vector<std::size_t> ends;
    std::array<Biquad, 2> highPass;
    std::size_t frames = 0;
    std::vector<WindowLevels> finished;
    Window current;
    std::vector<float> mono;

    void addSegment(std::span<const std::vector<float>> chunk, std::size_t begin, std::size_t length) {
        Window& window = current;
        if (window.spectra.empty()) {
            window.spectra.assign(channels, SpectrumSum{kToneFrame});
        }
        const std::size_t end = window.frames + length;
        window.stepSquares.resize((end + stepFrames - 1) / stepFrames);
        window.envelope.resize((end + hopFrames - 1) / hopFrames);
        mono.assign(length, 0.0F);
        for (std::size_t channelIndex = 0; channelIndex < chunk.size(); ++channelIndex) {
            const std::vector<float>& channel = chunk[channelIndex];
            window.spectra[channelIndex].add(std::span{channel}.subspan(begin, length));
            for (std::size_t index = 0; index < length; ++index) {
                const float sample = channel[begin + index];
                const double square = static_cast<double>(sample) * sample;
                window.squares += square;
                window.sum += sample;
                window.stepSquares[(window.frames + index) / stepFrames] += square;
                window.peak = std::max(window.peak, std::abs(sample));
                window.clipped += std::abs(sample) >= kClipLevel ? 1 : 0;
                mono[index] += sample / static_cast<float>(channels);
            }
        }
        for (std::size_t index = 0; index < length; ++index) {
            const double high = highPass[1].process(highPass[0].process(mono[index]));
            window.envelope[(window.frames + index) / hopFrames] += high * high;
        }
        window.monoSpectrum.add(mono);
        window.frames = end;
    }

    [[nodiscard]] WindowLevels levelsOf(const Window& window) const {
        const auto samples = static_cast<double>(window.frames * channels);
        const double rmsDb = powerDb(window.squares / samples);
        const double peakDb = amplitudeDb(window.peak);
        WindowLevels levels{.activeRmsDb = activeRmsDb(window.stepSquares, window.frames, stepFrames, channels),
                            .rmsDb = rmsDb,
                            .peakDb = peakDb,
                            .crestDb = peakDb - rmsDb,
                            .clipped = window.clipped,
                            .onsets = onsetsOf(window.envelope),
                            .mean = window.sum / samples};
        std::vector<double> channelPower((kToneFrame / 2) + 1);
        for (const SpectrumSum& spectrum : window.spectra) {
            std::ranges::transform(channelPower, spectrum.sum(), channelPower.begin(), std::plus{});
        }
        ToneBalance tone;
        setSpectrumTone(tone, channelPower, sampleRate);
        const std::vector<double> power = window.monoSpectrum.sum();
        levels.bandShare = tone.bandShare;
        levels.centroidHz = tone.centroidHz;
        const TonalPeak peak = tonalPeak(power, sampleRate);
        levels.tonalPeakHz = peak.hz;
        levels.tonality = peak.tonality;
        return levels;
    }
};

WindowMeter::WindowMeter(std::size_t channels, double sampleRate, std::vector<std::size_t> windowEnds)
    : state_(std::make_unique<State>(channels, sampleRate, std::move(windowEnds))) {}

WindowMeter::~WindowMeter() = default;
WindowMeter::WindowMeter(WindowMeter&&) noexcept = default;
WindowMeter& WindowMeter::operator=(WindowMeter&&) noexcept = default;

void WindowMeter::add(std::span<const std::vector<float>> chunk) {
    State& state = *state_;
    const std::size_t length = chunk.front().size();
    for (std::size_t done = 0; done < length && state.finished.size() < state.ends.size();) {
        const std::size_t take = std::min(state.ends[state.finished.size()] - state.frames, length - done);
        state.addSegment(chunk, done, take);
        state.frames += take;
        done += take;
        if (state.frames == state.ends[state.finished.size()]) {
            state.finished.push_back(state.levelsOf(state.current));
            state.current = Window{};
        }
    }
}

std::vector<WindowLevels> WindowMeter::windows() const {
    std::vector<WindowLevels> windows = state_->finished;
    if (state_->current.frames > 0) {
        windows.push_back(state_->levelsOf(state_->current));
    }
    return windows;
}

}  // namespace emerick::engine
