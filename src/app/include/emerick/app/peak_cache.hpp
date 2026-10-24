#pragma once

#include <QObject>
#include <QThreadPool>
#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
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

// Builds the coarser levels from the finest one.
PeakLevels buildLevels(Peaks finest);

// Where we keep a file's peaks on disk: "<folder>/.peaks/<file name>.peaks" beside the audio.
std::filesystem::path peakFile(const std::filesystem::path& audio);

// Reads the peak file when its recorded size and modification time still match the audio, else reads the audio and
// writes the peak file. A file that cannot be read gives empty peaks.
PeakLevels loadPeaks(const std::filesystem::path& audio);

// The lowest and highest sample of frames [from, to), read from the coarsest level that still resolves the span.
std::optional<PeakRange> peakRange(const PeakLevels& peaks, core::Frames from, core::Frames to);

// Loads each file's peaks once on its own thread pool and keeps them for the life of the cache.
class PeakCache : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Null while the peaks load; loaded() fires on the cache's thread once they are in.
    const PeakLevels* levels(const std::filesystem::path& file);
    // Adds the finest peaks of a take in progress, so its waveform draws while it records.
    void addLive(const std::filesystem::path& file, const Peaks& finest);
    // Drops a recorded file's live peaks, so the next request loads them from the finished file.
    void forgetLive(const std::filesystem::path& file);

signals:
    void loaded();

private:
    std::mutex mutex_;
    std::map<std::filesystem::path, Peaks> live_;
    // A null entry means the file is loading.
    std::map<std::filesystem::path, std::unique_ptr<const PeakLevels>> files_;
    // Declared last, so the pool waits for running loads before the map goes away.
    QThreadPool pool_;
};

}  // namespace emerick::app
