#include "emerick/engine/spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>
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

std::vector<double> averagePower(const std::vector<std::vector<float>>& channels, std::size_t frameSize) {
    std::vector<double> sum(frameSize / 2 + 1);
    std::vector<float> frame(frameSize);
    for (const std::vector<float>& channel : channels) {
        for (std::size_t begin = 0; begin < channel.size(); begin += frameSize) {
            const std::size_t count = std::min(frameSize, channel.size() - begin);
            std::ranges::fill(std::ranges::copy(std::span{channel}.subspan(begin, count), frame.begin()).out,
                              frame.end(), 0.0F);
            std::ranges::transform(sum, powerSpectrum(frame), sum.begin(), std::plus{});
        }
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

}  // namespace emerick::engine
