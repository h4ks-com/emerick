#pragma once

#include <QIODevice>
#include <QObject>
#include <QtTypes>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include "emerick/core/time.hpp"

namespace emerick::app {

// Frames per peak at each level, finest first. Each level folds four peaks of the one before.
inline constexpr std::array<core::Frames, 4> kFramesPerPeak{256, 1024, 4096, 16384};

// The lowest and highest sample of every block of frames, over all channels.
struct Peaks {
    std::vector<float> minimum;
    std::vector<float> maximum;
};

struct PeakLevels {
    std::array<Peaks, kFramesPerPeak.size()> levels;
};

struct PeakRange {
    float low = 0;
    float high = 0;
};

// What identifies the audio a peak file was made from.
struct AudioStamp {
    quint64 size = 0;
    qint64 modified = 0;
    bool operator==(const AudioStamp&) const = default;
};

// A peak file holds the finest peaks of one audio file and the stamp of that file.
struct PeakFile {
    AudioStamp stamp;
    Peaks peaks;
};

// Builds the coarser levels from the finest one.
PeakLevels buildLevels(Peaks finest);
// Refolds each coarser level from the group holding the first changed finest peak, so a growing take costs its tail.
void foldLevels(PeakLevels& peaks, std::size_t changed);

// Where we keep a file's peaks: "<folder>/.peaks/<file name>.peaks" beside the audio.
std::filesystem::path peakFile(const std::filesystem::path& audio);

std::optional<PeakFile> readPeakFile(QIODevice& input);
void writePeakFile(QIODevice& output, const PeakFile& file);

// The lowest and highest sample of frames [from, to), read from the coarsest level that still resolves the span.
std::optional<PeakRange> peakRange(const PeakLevels& peaks, core::Frames from, core::Frames to);

// Hands out the peaks of audio files by path: the app reads them from disk, the viewer fetches them over HTTP.
class PeakSource : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Null while the peaks load; loaded() fires on the source's thread once they are in.
    virtual const PeakLevels* levels(const std::filesystem::path& file) = 0;

signals:
    void loaded();
};

}  // namespace emerick::app
