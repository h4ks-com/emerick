#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "emerick/engine/audio_analysis.hpp"

namespace emerick::engine {

// Every level in dB is minus infinity for digital silence.
struct WindowLevels {
    // The median RMS of the window's 100 ms blocks above -60 dBFS, so minus infinity when none is that loud.
    double activeRmsDb = 0;
    double rmsDb = 0;
    double peakDb = 0;
    double crestDb = 0;
    std::array<double, kToneBands.size()> bandShare{};
    double centroidHz = 0;
    // The strongest narrow spectral peak above 1 kHz, or 0 when the window has none.
    double tonalPeakHz = 0;
    // The peak's share of the energy within an octave on either side of it, from 0 for noise to 1 for a pure tone.
    double tonality = 0;
    std::int64_t clipped = 0;
    // How often the level above 1 kHz rises from quiet to loud, as each hit of a beep does.
    int onsets = 0;
    // How long a hit above 1 kHz lasts on average, from its onset until it goes quiet. A beep holds its tone, while a
    // struck sound such as claves dies within a few tens of ms.
    double hitSeconds = 0;
    // The mean sample value over every channel, which a DC offset moves away from 0.
    double mean = 0;
};

// Measures a signal handed over in consecutive chunks in consecutive windows, so a long render never has to be held
// whole.
class WindowMeter {
public:
    // Window i ends at frame windowEnds[i] of the signal, counted from its first frame, and the ends ascend.
    WindowMeter(std::size_t channels, double sampleRate, std::vector<std::size_t> windowEnds);
    ~WindowMeter();
    WindowMeter(WindowMeter&&) noexcept;
    WindowMeter& operator=(WindowMeter&&) noexcept;
    WindowMeter(const WindowMeter&) = delete;
    WindowMeter& operator=(const WindowMeter&) = delete;

    // Takes one vector per channel, all of one length; frames past the last window end are left out.
    void add(std::span<const std::vector<float>> chunk);
    // The windows the signal has reached so far, the last one possibly cut short.
    [[nodiscard]] std::vector<WindowLevels> windows() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace emerick::engine
