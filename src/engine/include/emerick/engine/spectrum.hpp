#pragma once

#include <complex>
#include <span>
#include <vector>

namespace emerick::engine {

// Transforms in place with a radix-2 FFT; the size is a power of two.
void fft(std::span<std::complex<double>> data);

// Returns the unscaled power of each of the frame.size() / 2 + 1 bins of the Hann windowed frame. The frame size is a
// power of two.
std::vector<double> powerSpectrum(std::span<const float> frame);

// Returns the magnitude in dB of each of the frame.size() / 2 + 1 bins of the Hann windowed frame, scaled so a full
// scale sine on a bin reads 0 dB. The frame size is a power of two.
std::vector<double> spectrumDb(std::span<const float> frame);

}  // namespace emerick::engine
