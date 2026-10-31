#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace emerick::engine {

struct FrequencyBand {
    std::string_view name;
    double low = 0;
    double high = 0;
};

inline constexpr std::array<FrequencyBand, 7> kFrequencyBands{{{"sub", 20, 60},
                                                               {"bass", 60, 250},
                                                               {"lowMid", 250, 500},
                                                               {"mid", 500, 2000},
                                                               {"presence", 2000, 5000},
                                                               {"brilliance", 5000, 10000},
                                                               {"air", 10000, 20000}}};

struct BarLevel {
    int bar = 1;
    double rmsDb = 0;
};

struct SilentRegion {
    double start = 0;
    double end = 0;
};

struct StereoImage {
    double correlation = 0;
    // Positive when the left channel is louder.
    double balanceDb = 0;
};

// Every level in dB is minus infinity for digital silence.
struct AudioAnalysis {
    double integratedLufs = 0;
    double truePeakDbtp = 0;
    double samplePeakDbfs = 0;
    std::vector<BarLevel> bars;
    std::array<double, kFrequencyBands.size()> bandDb{};
    std::vector<SilentRegion> silence;
    std::int64_t clippedSamples = 0;
    std::optional<StereoImage> stereo;
};

struct AnalysisTiming {
    double sampleRate = 48000;
    double barSeconds = 2;
    // Where the first sample sits, which places it on the bar grid and offsets the silent regions.
    double startSeconds = 0;
};

inline constexpr std::array<FrequencyBand, 5> kToneBands{
    {{"low", 0, 120}, {"lowMid", 120, 500}, {"mid", 500, 2000}, {"highMid", 2000, 6000}, {"high", 6000, 1e9}}};

struct ToneBalance {
    // The median RMS of the 100 ms blocks above -60 dBFS, so silent stretches leave it alone.
    double activeRmsDb = 0;
    // Each band's share of the energy, summing to 1, or all 0 for silence.
    std::array<double, kToneBands.size()> bandShare{};
    double centroidHz = 0;
};

ToneBalance analyzeTone(const std::vector<std::vector<float>>& channels, double sampleRate);

// Measures loudness after ITU-R BS.1770-4 with every channel weighted 1, so a mono full scale 997 Hz sine reads
// -3.01 LUFS. Every channel has the same length.
AudioAnalysis analyzeAudio(const std::vector<std::vector<float>>& channels, const AnalysisTiming& timing);

}  // namespace emerick::engine
