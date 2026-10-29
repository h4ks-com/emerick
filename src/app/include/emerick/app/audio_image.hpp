#pragma once

#include <QImage>
#include <QSize>

#include "emerick/app/audio_file.hpp"

namespace emerick::app {

// Draws each channel's min and max per pixel column, channels stacked, with a faint line where each bar starts.
// startSeconds is where the first sample sits on the bar grid.
QImage waveformImage(const AudioData& audio, QSize size, double startSeconds, double barSeconds);

// Draws the mono mix's spectrum in dB per pixel column, 20 Hz at the bottom to 20 kHz at the top on a log axis.
QImage spectrogramImage(const AudioData& audio, QSize size);

}  // namespace emerick::app
