#include "emerick/engine/note_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <span>

#include "emerick/engine/audio_analysis.hpp"
#include "emerick/engine/spectrum.hpp"

namespace emerick::engine {

namespace {

constexpr std::size_t kNoteFrame = 16384;
constexpr double kLowestHz = 20;
constexpr double kHarmonicWidth = 0.1;
constexpr double kHarmonicBins = 3;

std::vector<double> summedPower(const std::vector<std::vector<float>>& channels) {
    std::vector<double> sum(kNoteFrame / 2 + 1);
    std::vector<float> frame(kNoteFrame);
    for (const std::vector<float>& channel : channels) {
        for (std::size_t begin = 0; begin < channel.size(); begin += kNoteFrame) {
            const std::size_t count = std::min(kNoteFrame, channel.size() - begin);
            std::ranges::fill(std::ranges::copy(std::span{channel}.subspan(begin, count), frame.begin()).out,
                              frame.end(), 0.0F);
            std::ranges::transform(sum, powerSpectrum(frame), sum.begin(), std::plus{});
        }
    }
    return sum;
}

double harmonicShare(const std::vector<double>& power, double binHz, double fundamentalHz) {
    const double halfWidth = std::max(kHarmonicWidth * fundamentalHz, kHarmonicBins * binHz);
    double harmonic = 0;
    double total = 0;
    for (std::size_t bin = 0; bin < power.size(); ++bin) {
        const double hz = static_cast<double>(bin) * binHz;
        if (hz < kLowestHz) {
            continue;
        }
        const double nearest = std::max(1.0, std::round(hz / fundamentalHz)) * fundamentalHz;
        total += power[bin];
        if (std::abs(hz - nearest) <= halfWidth) {
            harmonic += power[bin];
        }
    }
    return total == 0 ? 0 : harmonic / total;
}

}  // namespace

double noteHz(int note) {
    constexpr double kA4 = 440;
    constexpr int kA4Note = 69;
    constexpr double kSemitones = 12;
    return kA4 * std::exp2((note - kA4Note) / kSemitones);
}

NoteAnalysis analyzeNote(const std::vector<std::vector<float>>& channels, double sampleRate, double fundamentalHz) {
    float peak = 0;
    for (const std::vector<float>& channel : channels) {
        peak = std::ranges::fold_left(channel, peak,
                                      [](float loudest, float sample) { return std::max(loudest, std::abs(sample)); });
    }
    if (peak == 0) {
        const double silent = -std::numeric_limits<double>::infinity();
        return {.peakDb = silent, .activeRmsDb = silent, .centroidHz = 0, .harmonicShare = 0};
    }
    const ToneBalance tone = analyzeTone(channels, sampleRate);
    return {.peakDb = 20 * std::log10(peak),
            .activeRmsDb = tone.activeRmsDb,
            .centroidHz = tone.centroidHz,
            .harmonicShare =
                harmonicShare(summedPower(channels), sampleRate / static_cast<double>(kNoteFrame), fundamentalHz)};
}

}  // namespace emerick::engine
