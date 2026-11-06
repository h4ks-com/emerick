#include "emerick/engine/spectrum.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <numbers>
#include <numeric>
#include <ranges>
#include <utility>

namespace emerick::engine {

namespace {

constexpr double kFloorDb = -200;

// What a transform of one size reuses: each twiddle, exact from its angle, and the Hann window of a frame twice the
// size, which powerSpectrum packs into a transform of half its size.
struct FftPlan {
    std::size_t size = 0;
    std::vector<std::complex<double>> twiddles;
    std::vector<std::complex<double>> realTwiddles;
    std::vector<double> hann;
};

// The meters measure on threads that last one chunk, so every thread shares the plans. A deque keeps each plan we
// handed out in place as we add more.
const FftPlan& planOf(std::size_t size) {
    static std::mutex mutex;
    static std::deque<FftPlan> plans;
    const std::scoped_lock lock(mutex);
    const auto found = std::ranges::find(plans, size, &FftPlan::size);
    if (found != plans.end()) {
        return *found;
    }
    FftPlan plan{.size = size, .twiddles = {}, .realTwiddles = {}, .hann = {}};
    const auto angle = [](std::size_t index, std::size_t of) {
        return -2 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(of);
    };
    plan.twiddles = std::views::iota(std::size_t{0}, size / 2) |
                    std::views::transform([&](std::size_t index) { return std::polar(1.0, angle(index, size)); }) |
                    std::ranges::to<std::vector>();
    plan.realTwiddles =
        std::views::iota(std::size_t{0}, size + 1) |
        std::views::transform([&](std::size_t index) { return std::polar(1.0, angle(index, 2 * size)); }) |
        std::ranges::to<std::vector>();
    plan.hann =
        std::views::iota(std::size_t{0}, 2 * size) | std::views::transform([&](std::size_t index) {
            return 0.5 -
                   (0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(2 * size)));
        }) |
        std::ranges::to<std::vector>();
    plans.push_back(std::move(plan));
    return plans.back();
}

// Multiplies with the plain formula, since std::complex's operator* also handles infinite and NaN operands at a cost
// larger than the multiply.
std::complex<double> times(std::complex<double> a, std::complex<double> b) {
    return {(a.real() * b.real()) - (a.imag() * b.imag()), (a.real() * b.imag()) + (a.imag() * b.real())};
}

// Transforms in place with the plan of the data's size.
void transform(std::span<std::complex<double>> data, const FftPlan& plan) {
    const std::size_t size = data.size();
    for (std::size_t i = 1, j = 0; i < size; ++i) {
        std::size_t bit = size >> 1U;
        for (; (j & bit) != 0; bit >>= 1U) {
            j ^= bit;
        }
        j |= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }
    const std::vector<std::complex<double>>& twiddles = plan.twiddles;
    for (std::size_t length = 2; length <= size; length <<= 1U) {
        const std::size_t stride = size / length;
        for (std::size_t start = 0; start < size; start += length) {
            for (std::size_t k = 0; k < length / 2; ++k) {
                const std::complex<double> even = data[start + k];
                const std::complex<double> odd = times(data[start + k + (length / 2)], twiddles[k * stride]);
                data[start + k] = even + odd;
                data[start + k + (length / 2)] = even - odd;
            }
        }
    }
}

// Adds the power of each bin of the Hann windowed real frame to power, through a complex transform of half the frame
// that holds the even samples as real parts and the odd ones as imaginary parts.
void addPowerSpectrum(std::span<const float> frame, std::vector<std::complex<double>>& packed,
                      std::span<double> power) {
    const std::size_t half = frame.size() / 2;
    if (half == 0) {
        return;
    }
    const FftPlan& plan = planOf(half);
    packed.resize(half);
    for (std::size_t index = 0; index < half; ++index) {
        packed[index] = {plan.hann[2 * index] * frame[2 * index], plan.hann[(2 * index) + 1] * frame[(2 * index) + 1]};
    }
    transform(packed, plan);
    for (std::size_t bin = 0; bin <= half; ++bin) {
        const std::complex<double> here = packed[bin % half];
        const std::complex<double> mirror = std::conj(packed[(half - bin) % half]);
        const std::complex<double> even = 0.5 * (here + mirror);
        const std::complex<double> difference = here - mirror;
        const std::complex<double> odd{0.5 * difference.imag(), -0.5 * difference.real()};
        power[bin] += std::norm(even + times(plan.realTwiddles[bin], odd));
    }
}

}  // namespace

void fft(std::span<std::complex<double>> data) {
    if (data.size() >= 2) {
        transform(data, planOf(data.size()));
    }
}

std::vector<double> powerSpectrum(std::span<const float> frame) {
    std::vector<double> power((frame.size() / 2) + 1);
    std::vector<std::complex<double>> packed;
    addPowerSpectrum(frame, packed, power);
    return power;
}

SpectrumSum::SpectrumSum(std::size_t frameSize) : frameSize_(frameSize), sum_((frameSize / 2) + 1) {
    frame_.reserve(frameSize);
}

void SpectrumSum::add(std::span<const float> samples) {
    while (!samples.empty()) {
        const std::size_t count = std::min(frameSize_ - frame_.size(), samples.size());
        frame_.insert(frame_.end(), samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count));
        samples = samples.subspan(count);
        if (frame_.size() == frameSize_) {
            addPowerSpectrum(frame_, packed_, sum_);
            frame_.clear();
        }
    }
}

std::vector<double> SpectrumSum::sum() const {
    std::vector<double> sum = sum_;
    if (!frame_.empty()) {
        std::vector<float> padded = frame_;
        padded.resize(frameSize_, 0.0F);
        std::ranges::transform(sum, powerSpectrum(padded), sum.begin(), std::plus{});
    }
    return sum;
}

std::vector<double> averagePower(const std::vector<std::vector<float>>& channels, std::size_t frameSize) {
    std::vector<double> sum((frameSize / 2) + 1);
    for (const std::vector<float>& channel : channels) {
        SpectrumSum spectrum(frameSize);
        spectrum.add(channel);
        std::ranges::transform(sum, spectrum.sum(), sum.begin(), std::plus{});
    }
    return sum;
}

std::vector<double> spectrumDb(std::span<const float> frame) {
    // The Hann window sums to size / 2, and a real sine splits its amplitude between two bins.
    const double scale = 4.0 / static_cast<double>(frame.size());
    std::vector<double> db = powerSpectrum(frame);
    std::ranges::transform(db, db.begin(), [scale](double power) {
        const double magnitude = std::sqrt(power) * scale;
        return magnitude > 0 ? std::max(20 * std::log10(magnitude), kFloorDb) : kFloorDb;
    });
    return db;
}

std::vector<float> windowWeights(SpectrumWindow window, std::size_t size) {
    using Terms = std::array<double, 4>;
    const Terms terms = [window] {
        switch (window) {
            case SpectrumWindow::blackmanHarris:
                return Terms{0.35875, 0.48829, 0.14128, 0.01168};
            case SpectrumWindow::blackman:
                return Terms{0.42, 0.5, 0.08, 0.0};
            case SpectrumWindow::hann:
                return Terms{0.5, 0.5, 0.0, 0.0};
        }
        return Terms{0.5, 0.5, 0.0, 0.0};
    }();
    std::vector<float> weights(size);
    for (std::size_t i = 0; i < size; ++i) {
        const double phase = 2 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(size);
        weights[i] = static_cast<float>(terms[0] - (terms[1] * std::cos(phase)) + (terms[2] * std::cos(2 * phase)) -
                                        (terms[3] * std::cos(3 * phase)));
    }
    return weights;
}

double independentSlide(SpectrumWindow window) {
    switch (window) {
        case SpectrumWindow::blackmanHarris:
            return 0.2;
        case SpectrumWindow::blackman:
            return 0.225;
        case SpectrumWindow::hann:
            return 0.266;
    }
    return 0.25;
}

SpectrogramAnalyzer::SpectrogramAnalyzer(std::size_t fftSize, SpectrumWindow window, double sampleRate,
                                         double minRowsPerSecond)
    : weights_(windowWeights(window, fftSize)),
      // A real sine splits its amplitude between two bins, so a full scale one reads 0 dB.
      scale_(2.0 / std::accumulate(weights_.begin(), weights_.end(), 0.0)),
      sampleRate_(sampleRate),
      hop_(std::max<std::size_t>(
          1, std::min(static_cast<std::size_t>(independentSlide(window) * static_cast<double>(fftSize)),
                      static_cast<std::size_t>(sampleRate / minRowsPerSecond)))),
      history_(fftSize),
      untilRow_(fftSize),
      bins_(fftSize) {}

std::vector<std::vector<float>> SpectrogramAnalyzer::add(std::span<const float> samples) {
    std::vector<std::vector<float>> rows;
    for (const float sample : samples) {
        history_[write_] = sample;
        write_ = (write_ + 1) % history_.size();
        if (--untilRow_ == 0) {
            untilRow_ = hop_;
            rows.push_back(row());
        }
    }
    return rows;
}

std::vector<float> SpectrogramAnalyzer::row() {
    const std::size_t size = history_.size();
    for (std::size_t i = 0; i < size; ++i) {
        bins_[i] = static_cast<double>(history_[(write_ + i) % size] * weights_[i]);
    }
    fft(bins_);
    std::vector<float> db(size / 2);
    for (std::size_t bin = 0; bin < db.size(); ++bin) {
        const double magnitude = std::abs(bins_[bin]) * scale_;
        db[bin] = static_cast<float>(magnitude > 0 ? std::max(20 * std::log10(magnitude), kFloorDb) : kFloorDb);
    }
    return db;
}

std::vector<SpectrumPeak> findPeaks(std::span<const float> db, double binHz, std::size_t count, double floorDb) {
    constexpr std::size_t kSeparation = 3;
    std::vector<std::size_t> maxima;
    for (std::size_t bin = 1; bin + 1 < db.size(); ++bin) {
        if (db[bin] > floorDb && db[bin] > db[bin - 1] && db[bin] >= db[bin + 1]) {
            maxima.push_back(bin);
        }
    }
    std::ranges::sort(maxima, std::greater{}, [&](std::size_t bin) { return db[bin]; });
    std::vector<std::size_t> kept;
    for (const std::size_t bin : maxima) {
        if (kept.size() == count) {
            break;
        }
        if (std::ranges::none_of(
                kept, [bin](std::size_t other) { return (bin > other ? bin - other : other - bin) < kSeparation; })) {
            kept.push_back(bin);
        }
    }
    return kept | std::views::transform([&](std::size_t bin) {
               const double left = db[bin - 1];
               const double middle = db[bin];
               const double right = db[bin + 1];
               const double curve = left - (2 * middle) + right;
               const double shift = curve < 0 ? 0.5 * (left - right) / curve : 0.0;
               return SpectrumPeak{.hz = (static_cast<double>(bin) + shift) * binHz,
                                   .db = middle - (0.25 * (left - right) * shift)};
           }) |
           std::ranges::to<std::vector>();
}

}  // namespace emerick::engine
