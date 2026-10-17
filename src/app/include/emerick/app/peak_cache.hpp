#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>
#include <filesystem>
#include <map>
#include <mutex>
#include <vector>

#include "emerick/core/time.hpp"

namespace emerick::app {

inline constexpr core::Frames kFramesPerPeak = 256;

// The lowest and highest sample of every kFramesPerPeak frames, over all channels.
struct Peaks {
    std::vector<float> minimum;
    std::vector<float> maximum;
};

// Reads each file once, on whichever thread first asks, and keeps its peaks for the life of the cache.
class PeakCache {
public:
    // A file that cannot be read gives empty peaks. The reference stays valid while the cache lives.
    const Peaks& peaks(const std::filesystem::path& file);

private:
    // ponytail: one lock over reading, so two files load one after the other; lock per file if that shows up.
    std::mutex mutex_;
    std::map<std::filesystem::path, Peaks> files_;
};

// Draws frames [offset, offset + frames) of the peaks as one opaque color on a transparent image.
QImage drawPeaks(const Peaks& peaks, core::Frames offset, core::Frames frames, QSize size);

// The image source QML gives an Image to show that stretch of the file.
QString peakSource(const std::filesystem::path& file, core::Frames offset, core::Frames frames);

// Serves "image://peaks/<offset>/<frames>/<hex path>". QML loads it on its image threads when asynchronous is set.
class PeakImageProvider : public QQuickImageProvider {
public:
    explicit PeakImageProvider(PeakCache& cache) : QQuickImageProvider(QQuickImageProvider::Image), cache_(cache) {}

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    PeakCache& cache_;
};

}  // namespace emerick::app
