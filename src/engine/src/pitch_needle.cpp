#include "emerick/engine/pitch_needle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace emerick::engine {

namespace {

constexpr double kHoldSeconds = 1.5;
constexpr double kEaseSeconds = 0.15;
constexpr double kSameNote = 0.5;
constexpr std::size_t kMedianReadings = 5;
constexpr double kPluckRise = 1.25;
constexpr std::size_t kFrameReadings = 4;
constexpr int kA4 = 69;
constexpr double kSemitones = 12.0;
constexpr double kThirdHarmonicSemitones = 19.019550008653874;
constexpr std::array kOvertones{kSemitones, kThirdHarmonicSemitones};

double median(std::vector<double> values) {
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::ranges::nth_element(values, middle);
    return *middle;
}

}  // namespace

double PitchNeedle::folded(double heard) const {
    if (!shown_ || readingsSincePluck_ < kMedianReadings) {
        return heard;
    }
    const auto* const overtone = std::ranges::find_if(
        kOvertones, [&](double above) { return std::abs(heard - above - semitones_) < kSameNote; });
    return overtone == kOvertones.end() ? heard : heard - *overtone;
}

void PitchNeedle::feed(const std::optional<PitchReading>& reading, double seconds, double referenceHz) {
    if (!reading) {
        pending_.reset();
        recent_.clear();
        energies_.clear();
        silentSeconds_ += seconds;
        if (silentSeconds_ > kHoldSeconds) {
            shown_ = false;
        }
        return;
    }
    silentSeconds_ = 0.0;
    confidence_ = reading->confidence;
    // Readings overlap, so we compare against one a whole frame back to see a soft attack.
    if (!energies_.empty() && reading->meanSquare > kPluckRise * energies_.front()) {
        readingsSincePluck_ = 0;
        recent_.clear();
    }
    ++readingsSincePluck_;
    energies_.push_back(reading->meanSquare);
    if (energies_.size() > kFrameReadings) {
        energies_.erase(energies_.begin());
    }
    recent_.push_back(folded(kA4 + (kSemitones * std::log2(reading->hz / referenceHz))));
    if (recent_.size() > kMedianReadings) {
        recent_.erase(recent_.begin());
    }
    const double target = median(recent_);
    if (shown_ && std::abs(target - semitones_) < kSameNote) {
        semitones_ += (target - semitones_) * (1.0 - std::exp(-seconds / kEaseSeconds));
        pending_.reset();
    } else if (pending_ && std::abs(target - *pending_) < kSameNote) {
        semitones_ = target;
        shown_ = true;
        pending_.reset();
    } else {
        pending_ = target;
    }
}

}  // namespace emerick::engine
