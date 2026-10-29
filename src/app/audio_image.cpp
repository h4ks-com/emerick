#include "emerick/app/audio_image.hpp"

#include <QColor>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "emerick/engine/spectrum.hpp"

namespace emerick::app {

namespace {

constexpr QColor kBackground{255, 255, 255};
constexpr QColor kWave{48, 48, 48};
constexpr QColor kBarLine{215, 215, 215};
constexpr std::size_t kSpectrumFrame = 4096;
constexpr double kLowestHz = 20;
constexpr double kHighestHz = 20000;
constexpr double kRangeDb = 80;

std::size_t frameCount(const AudioData& audio) { return audio.channels.empty() ? 0 : audio.channels.front().size(); }

std::size_t columnStart(int column, std::size_t frames, int width) {
    return static_cast<std::size_t>(column) * frames / static_cast<std::size_t>(width);
}

void drawBarLines(QPainter& painter, QSize size, double seconds, double startSeconds, double barSeconds) {
    painter.setPen(kBarLine);
    const auto lastBar = static_cast<long>(std::ceil((startSeconds + seconds) / barSeconds));
    for (auto bar = static_cast<long>(std::ceil(startSeconds / barSeconds)); bar < lastBar; ++bar) {
        const double at = static_cast<double>(bar) * barSeconds;
        const int x = static_cast<int>(std::lround((at - startSeconds) / seconds * size.width()));
        painter.drawLine(x, 0, x, size.height() - 1);
    }
}

void drawChannel(QPainter& painter, const std::vector<float>& samples, int width, double top, double laneHeight) {
    const double middle = top + (laneHeight / 2);
    const double half = laneHeight / 2;
    for (int column = 0; column < width; ++column) {
        const std::size_t first = columnStart(column, samples.size(), width);
        const std::size_t last = std::max(first + 1, columnStart(column + 1, samples.size(), width));
        const auto [low, high] = std::ranges::minmax(std::span{samples}.subspan(first, last - first));
        painter.drawLine(column, static_cast<int>(std::lround(middle - (std::clamp(high, -1.0F, 1.0F) * half))), column,
                         static_cast<int>(std::lround(middle - (std::clamp(low, -1.0F, 1.0F) * half))));
    }
}

std::vector<float> monoMix(const AudioData& audio) {
    std::vector<float> mono(frameCount(audio));
    for (const std::vector<float>& channel : audio.channels) {
        std::ranges::transform(mono, channel, mono.begin(), [](float sum, float sample) { return sum + sample; });
    }
    const auto scale = 1.0F / static_cast<float>(std::max<std::size_t>(audio.channels.size(), 1));
    std::ranges::transform(mono, mono.begin(), [scale](float sample) { return sample * scale; });
    return mono;
}

std::vector<float> frameAround(const std::vector<float>& samples, std::size_t center) {
    std::vector<float> frame(kSpectrumFrame);
    const auto start = static_cast<std::ptrdiff_t>(center) - static_cast<std::ptrdiff_t>(kSpectrumFrame / 2);
    for (std::size_t i = 0; i < kSpectrumFrame; ++i) {
        const std::ptrdiff_t at = start + static_cast<std::ptrdiff_t>(i);
        if (at >= 0 && std::cmp_less(at, samples.size())) {
            frame[i] = samples[static_cast<std::size_t>(at)];
        }
    }
    return frame;
}

}  // namespace

QImage waveformImage(const AudioData& audio, QSize size, double startSeconds, double barSeconds) {
    QImage image(size, QImage::Format_RGB32);
    image.fill(kBackground);
    const std::size_t frames = frameCount(audio);
    if (frames == 0) {
        return image;
    }
    QPainter painter(&image);
    drawBarLines(painter, size, static_cast<double>(frames) / audio.sampleRate, startSeconds, barSeconds);
    painter.setPen(kWave);
    const double laneHeight = static_cast<double>(size.height()) / static_cast<double>(audio.channels.size());
    for (std::size_t index = 0; index < audio.channels.size(); ++index) {
        drawChannel(painter, audio.channels[index], size.width(), static_cast<double>(index) * laneHeight, laneHeight);
    }
    return image;
}

QImage spectrogramImage(const AudioData& audio, QSize size) {
    QImage image(size, QImage::Format_Grayscale8);
    image.fill(Qt::black);
    const std::vector<float> mono = monoMix(audio);
    if (mono.empty()) {
        return image;
    }
    const double binsPerHz = static_cast<double>(kSpectrumFrame) / audio.sampleRate;
    const int lastRow = std::max(size.height() - 1, 1);
    for (int column = 0; column < size.width(); ++column) {
        const std::size_t center =
            (columnStart(column, mono.size(), size.width()) + columnStart(column + 1, mono.size(), size.width())) / 2;
        const std::vector<double> db = engine::spectrumDb(frameAround(mono, center));
        for (int row = 0; row < size.height(); ++row) {
            const double hz =
                kLowestHz * std::pow(kHighestHz / kLowestHz, static_cast<double>(lastRow - row) / lastRow);
            const auto bin = static_cast<std::size_t>(std::lround(hz * binsPerHz));
            if (bin < db.size()) {
                const int gray =
                    static_cast<int>(std::lround(std::clamp((db[bin] + kRangeDb) / kRangeDb, 0.0, 1.0) * 255));
                image.setPixelColor(column, row, QColor{gray, gray, gray});
            }
        }
    }
    return image;
}

}  // namespace emerick::app
