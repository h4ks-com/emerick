#pragma once

#include <QThreadPool>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include "emerick/app/peak_levels.hpp"

namespace emerick::app {

// Reads the peak file when its recorded size and modification time still match the audio, else reads the audio and
// writes the peak file. A file that cannot be read gives empty peaks.
PeakLevels loadPeaks(const std::filesystem::path& audio);

// Loads each file's peaks once on its own thread pool and keeps them for the life of the cache.
class PeakCache : public PeakSource {
    Q_OBJECT

public:
    using PeakSource::PeakSource;

    const PeakLevels* levels(const std::filesystem::path& file) override;
    // Adds the finest peaks of a take in progress, so its waveform draws while it records.
    void addLive(const std::filesystem::path& file, const Peaks& finest);
    // Drops a recorded file's live peaks, so the next request loads them from the finished file.
    void forgetLive(const std::filesystem::path& file);

private:
    std::mutex mutex_;
    std::set<std::filesystem::path> live_;
    // A null entry means the file is loading.
    std::map<std::filesystem::path, std::unique_ptr<PeakLevels>> files_;
    // Declared last, so the pool waits for running loads before the map goes away.
    QThreadPool pool_;
};

}  // namespace emerick::app
