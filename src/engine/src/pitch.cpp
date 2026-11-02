#include "emerick/engine/pitch.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

#include "emerick/engine/spectrum.hpp"

namespace emerick::engine {

namespace {

// The first key maximum this close to the highest one is the period, which keeps us off the octaves below it.
constexpr double kPeakShare = 0.9;
constexpr double kMinConfidence = 0.8;
constexpr double kSilentMeanSquare = 1e-7;
constexpr int kSemitones = 12;
constexpr int kA4 = 69;
constexpr double kCentsPerSemitone = 100.0;
constexpr std::array kNoteNames{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Sums frame[j] * frame[j + lag] for each lag through an FFT of the frame padded to twice its size.
std::vector<double> autocorrelation(std::span<const float> frame) {
    const std::size_t size = std::bit_ceil(frame.size() * 2);
    std::vector<std::complex<double>> bins(size);
    std::ranges::copy(frame, bins.begin());
    fft(bins);
    for (std::complex<double>& bin : bins) {
        bin = std::norm(bin);
    }
    fft(bins);
    std::vector<double> sums(frame.size());
    const double scale = 1.0 / static_cast<double>(size);
    for (std::size_t lag = 0; lag < sums.size(); ++lag) {
        sums[lag] = bins[lag].real() * scale;
    }
    return sums;
}

// The normalized square difference function of the McLeod pitch method, which reads 1 at the lag of a period.
std::vector<double> squareDifference(std::span<const float> frame, std::size_t maxLag) {
    const std::vector<double> sums = autocorrelation(frame);
    std::vector<double> nsdf(maxLag + 1);
    double energy = 0.0;
    for (const float sample : frame) {
        energy += 2.0 * static_cast<double>(sample) * static_cast<double>(sample);
    }
    const std::size_t size = frame.size();
    for (std::size_t lag = 0; lag <= maxLag; ++lag) {
        if (lag > 0) {
            const auto first = static_cast<double>(frame[lag - 1]);
            const auto last = static_cast<double>(frame[size - lag]);
            energy -= (first * first) + (last * last);
        }
        nsdf[lag] = energy > 0.0 ? 2.0 * sums[lag] / energy : 0.0;
    }
    return nsdf;
}

struct Peak {
    double lag = 0.0;
    double value = 0.0;
};

Peak refinedPeak(std::span<const double> nsdf, std::size_t lag) {
    const double left = nsdf[lag - 1];
    const double middle = nsdf[lag];
    const double right = nsdf[lag + 1];
    const double curve = left - (2.0 * middle) + right;
    if (curve >= 0.0) {
        return {.lag = static_cast<double>(lag), .value = middle};
    }
    const double shift = 0.5 * (left - right) / curve;
    return {.lag = static_cast<double>(lag) + shift, .value = middle - (0.25 * (left - right) * shift)};
}

// The highest point of each stretch where the function is positive, after it first falls below zero.
std::vector<Peak> keyMaxima(std::span<const double> nsdf, std::size_t minLag) {
    std::vector<Peak> peaks;
    std::size_t lag = 1;
    while (lag + 1 < nsdf.size() && nsdf[lag] > 0.0) {
        ++lag;
    }
    std::size_t best = 0;
    for (; lag + 1 < nsdf.size(); ++lag) {
        if (nsdf[lag] > 0.0 && nsdf[lag - 1] <= 0.0) {
            best = lag;
        }
        if (best != 0 && nsdf[lag] > nsdf[best]) {
            best = lag;
        }
        if (best != 0 && nsdf[lag] <= 0.0) {
            if (best >= minLag) {
                peaks.push_back(refinedPeak(nsdf, best));
            }
            best = 0;
        }
    }
    if (best >= minLag && best + 1 < nsdf.size()) {
        peaks.push_back(refinedPeak(nsdf, best));
    }
    return peaks;
}

// The parabola's error is a fraction of a sample on any one peak, so we measure the furthest multiple of the period
// the frame holds and divide, which divides that error too.
double refinedPeriod(std::span<const double> nsdf, const Peak& period) {
    const auto repeats = static_cast<std::size_t>(static_cast<double>(nsdf.size() - 2) / period.lag);
    if (repeats < 2) {
        return period.lag;
    }
    const double around = period.lag * static_cast<double>(repeats);
    const auto reach = static_cast<std::size_t>(period.lag / 4);
    const auto from = static_cast<std::size_t>(around) - reach;
    const std::size_t to = std::min(static_cast<std::size_t>(around) + reach, nsdf.size() - 2);
    std::size_t best = from;
    for (std::size_t lag = from; lag <= to; ++lag) {
        if (nsdf[lag] > nsdf[best]) {
            best = lag;
        }
    }
    const Peak far = refinedPeak(nsdf, best);
    return far.value >= kPeakShare * period.value ? far.lag / static_cast<double>(repeats) : period.lag;
}

}  // namespace

std::optional<PitchReading> detectPitch(std::span<const float> frame, double sampleRate) {
    double meanSquare = 0.0;
    for (const float sample : frame) {
        meanSquare += static_cast<double>(sample) * static_cast<double>(sample);
    }
    if (frame.size() < 4 || meanSquare / static_cast<double>(frame.size()) < kSilentMeanSquare) {
        return std::nullopt;
    }
    const auto minLag = static_cast<std::size_t>(sampleRate / kHighestPitchHz);
    const std::size_t maxLag = std::min(static_cast<std::size_t>(sampleRate / kLowestPitchHz), (frame.size() / 2) - 1);
    const std::vector<double> nsdf = squareDifference(frame, (frame.size() / 2) - 1);
    const std::vector<Peak> peaks = keyMaxima(std::span(nsdf).first(maxLag + 1), std::max<std::size_t>(minLag, 2));
    if (peaks.empty()) {
        return std::nullopt;
    }
    const double highest = std::ranges::max(peaks, {}, &Peak::value).value;
    const Peak& period =
        *std::ranges::find_if(peaks, [&](const Peak& peak) { return peak.value >= kPeakShare * highest; });
    if (period.value < kMinConfidence) {
        return std::nullopt;
    }
    return PitchReading{.hz = sampleRate / refinedPeriod(nsdf, period), .confidence = std::min(period.value, 1.0)};
}

NoteReading nearestNote(double hz, double referenceHz) {
    const double semitones = kA4 + (kSemitones * std::log2(hz / referenceHz));
    const double nearest = std::round(semitones);
    return {.midiNote = static_cast<int>(nearest), .cents = (semitones - nearest) * kCentsPerSemitone};
}

std::string pitchClassName(int midiNote) {
    return kNoteNames.at(static_cast<std::size_t>(((midiNote % kSemitones) + kSemitones) % kSemitones));
}

int noteOctave(int midiNote) { return static_cast<int>(std::floor(midiNote / static_cast<double>(kSemitones))) - 1; }

std::string noteName(int midiNote) { return pitchClassName(midiNote) + std::to_string(noteOctave(midiNote)); }

}  // namespace emerick::engine
