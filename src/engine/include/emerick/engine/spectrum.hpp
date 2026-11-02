#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace emerick::engine {

// Transforms in place with a radix-2 FFT; the size is a power of two.
void fft(std::span<std::complex<double>> data);

// Returns the unscaled power of each of the frame.size() / 2 + 1 bins of the Hann windowed frame. The frame size is a
// power of two.
std::vector<double> powerSpectrum(std::span<const float> frame);
// Sums powerSpectrum over consecutive frames of frameSize samples of one signal handed over in chunks, the last frame
// padded with silence. The frame size is a power of two.
class SpectrumSum {
public:
    explicit SpectrumSum(std::size_t frameSize);
    void add(std::span<const float> samples);
    [[nodiscard]] std::vector<double> sum() const;

private:
    std::size_t frameSize_;
    std::vector<float> frame_;
    std::vector<double> sum_;
};

// Returns powerSpectrum summed over consecutive frames of frameSize samples of every channel, the last frame padded
// with silence. The frame size is a power of two.
std::vector<double> averagePower(const std::vector<std::vector<float>>& channels, std::size_t frameSize);

// Returns the magnitude in dB of each of the frame.size() / 2 + 1 bins of the Hann windowed frame, scaled so a full
// scale sine on a bin reads 0 dB. The frame size is a power of two.
std::vector<double> spectrumDb(std::span<const float> frame);

enum class SpectrumWindow : std::uint8_t { blackmanHarris, blackman, hann };

// The window's weights for a frame of size samples.
std::vector<float> windowWeights(SpectrumWindow window, std::size_t size);
// How far a frame may slide, as a share of its size, before consecutive frames stop sharing what they measure.
double independentSlide(SpectrumWindow window);

// Cuts a signal handed over in chunks into overlapping windowed frames and returns each frame's spectrum as a row of
// fftSize / 2 levels in dB, scaled so a full scale sine on a bin reads 0 dB.
class SpectrogramAnalyzer {
public:
    // Slides each frame by the window's independent slide, or less so at least minRowsPerSecond rows come out.
    SpectrogramAnalyzer(std::size_t fftSize, SpectrumWindow window, double sampleRate, double minRowsPerSecond);

    std::vector<std::vector<float>> add(std::span<const float> samples);
    [[nodiscard]] std::size_t fftSize() const { return weights_.size(); }
    [[nodiscard]] std::size_t hop() const { return hop_; }
    [[nodiscard]] double binHz() const { return sampleRate_ / static_cast<double>(weights_.size()); }

private:
    std::vector<float> row();

    std::vector<float> weights_;
    double scale_ = 1.0;
    double sampleRate_ = 0.0;
    std::size_t hop_ = 0;
    std::vector<float> history_;
    std::size_t write_ = 0;
    std::size_t untilRow_ = 0;
    std::vector<std::complex<double>> bins_;
};

struct SpectrumPeak {
    double hz = 0.0;
    double db = 0.0;
};

// The count loudest local maxima of a row of levels in dB above floorDb, loudest first, each placed between bins by a
// parabola and at least three bins from a louder one.
std::vector<SpectrumPeak> findPeaks(std::span<const float> db, double binHz, std::size_t count, double floorDb);

}  // namespace emerick::engine
