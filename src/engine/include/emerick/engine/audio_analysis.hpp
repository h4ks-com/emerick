#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
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

struct AnalysisTiming {
    double sampleRate = 48000;
    double barSeconds = 2;
    // Where the first sample sits, which places it on the bar grid and offsets the silent regions.
    double startSeconds = 0;
};

// Five broad bands for a track's tone balance, named apart from kFrequencyBands so a result never mixes the two up.
inline constexpr std::array<FrequencyBand, 5> kToneBands{{{"low", 0, 120},
                                                          {"body", 120, 500},
                                                          {"middle", 500, 2000},
                                                          {"edge", 2000, 6000},
                                                          {"top", 6000, std::numeric_limits<double>::infinity()}}};

struct ToneBalance {
    // The median RMS of the 100 ms blocks above -60 dBFS, so silent stretches leave it alone.
    double activeRmsDb = 0;
    // Each band's share of the energy, summing to 1, or all 0 for silence.
    std::array<double, kToneBands.size()> bandShare{};
    double centroidHz = 0;
};

ToneBalance analyzeTone(const std::vector<std::vector<float>>& channels, double sampleRate);

// Every level in dB is minus infinity for digital silence.
struct TrackLevels {
    double integratedLufs = 0;
    double samplePeakDbfs = 0;
    double truePeakDbtp = 0;
    ToneBalance tone;
};

// Every level in dB is minus infinity for digital silence.
struct AudioAnalysis {
    TrackLevels levels;
    std::vector<BarLevel> bars;
    std::array<double, kFrequencyBands.size()> bandDb{};
    std::vector<SilentRegion> silence;
    std::int64_t clippedSamples = 0;
    std::optional<StereoImage> stereo;
};

// Measures the levels analyzeAudio and analyzeTone report of a signal handed over in consecutive chunks, so a long
// render never has to be held whole.
class TrackMeter {
public:
    TrackMeter(std::size_t channels, double sampleRate);
    ~TrackMeter();
    TrackMeter(TrackMeter&&) noexcept;
    TrackMeter& operator=(TrackMeter&&) noexcept;
    TrackMeter(const TrackMeter&) = delete;
    TrackMeter& operator=(const TrackMeter&) = delete;

    // Takes one vector per channel, all of one length.
    void add(std::span<const std::vector<float>> chunk);
    [[nodiscard]] TrackLevels levels() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Measures the integrated loudness TrackMeter reports of a signal handed over in consecutive chunks.
class LoudnessMeter {
public:
    LoudnessMeter(std::size_t channels, double sampleRate);
    ~LoudnessMeter();
    LoudnessMeter(LoudnessMeter&&) noexcept;
    LoudnessMeter& operator=(LoudnessMeter&&) noexcept;
    LoudnessMeter(const LoudnessMeter&) = delete;
    LoudnessMeter& operator=(const LoudnessMeter&) = delete;

    // Takes one vector per channel, all of one length.
    void add(std::span<const std::vector<float>> chunk);
    [[nodiscard]] double integratedLufs() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Measures loudness after ITU-R BS.1770-4 with every channel weighted 1, so a mono full scale 997 Hz sine reads
// -3.01 LUFS. Every channel has the same length.
AudioAnalysis analyzeAudio(const std::vector<std::vector<float>>& channels, const AnalysisTiming& timing);

// The integrated loudness and the true peak analyzeAudio reports, measured alone.
double integratedLufs(const std::vector<std::vector<float>>& channels, double sampleRate);
double truePeakDbtp(const std::vector<std::vector<float>>& channels);

// Takes one chunk of a signal, one vector per channel, all of one length.
using ChunkSink = std::function<void(std::span<const std::vector<float>>)>;
// Hands the same signal to the sink chunk by chunk each time it runs.
using SignalReplay = std::function<void(const ChunkSink&)>;

// Plays the signal turned by gainDb through the limiter insert's algorithm with its latency dropped, so the sink gets
// as many frames as the signal has, lined up with it.
void limitSignal(const SignalReplay& signal, std::size_t channels, double sampleRate, double gainDb, double ceilingDbtp,
                 const ChunkSink& sink);

struct LoudnessMatch {
    // Missing for digital silence, which stays as it is.
    std::optional<double> gainDb;
    // The integrated loudness the signal ends at.
    double integratedLufs = 0;
};

// Finds the gain that turns the signal to targetLufs once limitSignal keeps its true peak at or below ceilingDbtp. It
// replays the signal once to measure it and once per correction.
LoudnessMatch matchLoudness(const SignalReplay& signal, std::size_t channels, double sampleRate, double targetLufs,
                            double ceilingDbtp);

}  // namespace emerick::engine
