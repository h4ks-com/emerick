#include "emerick/engine/spectrum.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>
#include <numeric>
#include <ranges>
#include <utility>

namespace emerick::engine {

namespace {

constexpr double kFloorDb = -200;

}  // namespace

void fft(std::span<std::complex<double>> data) {
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
    for (std::size_t length = 2; length <= size; length <<= 1U) {
        const std::complex<double> step = std::polar(1.0, -2 * std::numbers::pi / static_cast<double>(length));
        for (std::size_t start = 0; start < size; start += length) {
            std::complex<double> twiddle = 1;
            for (std::size_t k = 0; k < length / 2; ++k) {
                const std::complex<double> even = data[start + k];
                const std::complex<double> odd = data[start + k + (length / 2)] * twiddle;
                data[start + k] = even + odd;
                data[start + k + (length / 2)] = even - odd;
                twiddle *= step;
            }
        }
    }
}

std::vector<double> powerSpectrum(std::span<const float> frame) {
    const std::size_t size = frame.size();
    std::vector<std::complex<double>> bins(size);
    for (std::size_t i = 0; i < size; ++i) {
        const double hann =
            0.5 - (0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(size)));
        bins[i] = hann * frame[i];
    }
    fft(bins);
    std::vector<double> power(size / 2 + 1);
    std::ranges::transform(bins.begin(), bins.begin() + static_cast<std::ptrdiff_t>(power.size()), power.begin(),
                           [](const std::complex<double>& bin) { return std::norm(bin); });
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
            std::ranges::transform(sum_, powerSpectrum(frame_), sum_.begin(), std::plus{});
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
