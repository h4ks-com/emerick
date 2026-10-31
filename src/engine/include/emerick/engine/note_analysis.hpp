#pragma once

#include <vector>

namespace emerick::engine {

// Every level in dB is minus infinity for digital silence.
struct NoteAnalysis {
    double peakDb = 0;
    // The median RMS of the 100 ms blocks above -60 dBFS, as ToneBalance measures it.
    double activeRmsDb = 0;
    double centroidHz = 0;
    // The share of the energy above 20 Hz within a tenth of the fundamental, or 3 bins, of a multiple of the
    // fundamental, or 0 for silence. A clean tone reads near 1 and noise near the share of the spectrum those windows
    // cover.
    double harmonicShare = 0;
};

// The equal tempered pitch of a MIDI note with A at note 69 on 440 Hz.
double noteHz(int note);

// Measures one played note of the channels, which all have the same length.
NoteAnalysis analyzeNote(const std::vector<std::vector<float>>& channels, double sampleRate, double fundamentalHz);

}  // namespace emerick::engine
