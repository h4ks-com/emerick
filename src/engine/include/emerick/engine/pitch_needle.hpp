#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "emerick/engine/pitch.hpp"

namespace emerick::engine {

// Keeps a tuner's needle steady. It shows the median of the last few readings, eases toward each new reading of the
// same note, moves to a new note once two readings agree on it, keeps the steady note through a reading an octave or a
// twelfth above it once a pluck has settled, and holds the last note for a moment of silence.
class PitchNeedle {
public:
    void feed(const std::optional<PitchReading>& reading, double seconds, double referenceHz);
    [[nodiscard]] bool shown() const { return shown_; }
    [[nodiscard]] bool holding() const { return shown_ && silentSeconds_ > 0.0; }
    // In semitones from MIDI note 0, between notes when the pitch sits between them.
    [[nodiscard]] double semitones() const { return semitones_; }
    [[nodiscard]] double confidence() const { return confidence_; }

private:
    [[nodiscard]] double folded(double heard) const;

    bool shown_ = false;
    double semitones_ = 0.0;
    double confidence_ = 0.0;
    double meanSquare_ = 0.0;
    double silentSeconds_ = 0.0;
    std::size_t readingsSincePluck_ = 0;
    std::optional<double> pending_;
    std::vector<double> recent_;
};

}  // namespace emerick::engine
