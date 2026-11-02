#include "emerick/app/peak_cache.hpp"

#include <sndfile.h>

#include <QFile>
#include <QSaveFile>
#include <algorithm>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "emerick/engine/sound_file.hpp"

namespace emerick::app {

namespace {

constexpr core::Frames kBlocksPerRead = 64;

using engine::SndFile;

std::optional<AudioStamp> stampOf(const std::filesystem::path& audio) {
    std::error_code error;
    const auto size = std::filesystem::file_size(audio, error);
    const auto modified = std::filesystem::last_write_time(audio, error);
    if (error) {
        return std::nullopt;
    }
    return AudioStamp{size, static_cast<qint64>(modified.time_since_epoch().count())};
}

Peaks readAudioPeaks(const std::filesystem::path& audio) {
    SF_INFO info{};
    const SndFile sound{engine::openSoundFile(audio, SFM_READ, &info)};
    if (!sound || info.channels <= 0) {
        return {};
    }
    const auto channels = static_cast<std::size_t>(info.channels);
    const core::Frames block = kFramesPerPeak.front();
    std::vector<float> buffer(static_cast<std::size_t>(block * kBlocksPerRead) * channels);
    Peaks peaks;
    while (true) {
        const sf_count_t read = sf_readf_float(sound.get(), buffer.data(), block * kBlocksPerRead);
        if (read <= 0) {
            break;
        }
        const auto samples = std::span{buffer}.first(static_cast<std::size_t>(read) * channels);
        const auto blockSamples = static_cast<std::size_t>(block) * channels;
        for (std::size_t at = 0; at < samples.size(); at += blockSamples) {
            const auto [low, high] =
                std::ranges::minmax(samples.subspan(at, std::min(blockSamples, samples.size() - at)));
            peaks.minimum.push_back(low);
            peaks.maximum.push_back(high);
        }
    }
    return peaks;
}

std::optional<Peaks> storedPeaks(const std::filesystem::path& file, const AudioStamp& stamp) {
    QFile input{QString::fromStdString(file.string())};
    if (!input.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    auto stored = readPeakFile(input);
    if (!stored || stored->stamp != stamp) {
        return std::nullopt;
    }
    return std::move(stored->peaks);
}

// We drop the peak file when the folder is read-only, because the peaks still work from memory.
void storePeaks(const std::filesystem::path& file, const PeakFile& peaks) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    QSaveFile output{QString::fromStdString(file.string())};
    if (error || !output.open(QIODevice::WriteOnly)) {
        return;
    }
    writePeakFile(output, peaks);
    output.commit();
}

}  // namespace

PeakLevels loadPeaks(const std::filesystem::path& audio) {
    const auto stamp = stampOf(audio);
    if (!stamp) {
        return {};
    }
    if (auto stored = storedPeaks(peakFile(audio), *stamp)) {
        return buildLevels(*std::move(stored));
    }
    PeakFile file{.stamp = *stamp, .peaks = readAudioPeaks(audio)};
    storePeaks(peakFile(audio), file);
    return buildLevels(std::move(file.peaks));
}

std::vector<std::string> writePeaks(const model::Document& document, const std::filesystem::path& folder) {
    std::set<std::filesystem::path> files;
    for (const model::Track& track : document.tracks) {
        for (const model::Clip& clip : track.clips) {
            if (const auto* audio = std::get_if<model::AudioClip>(&clip)) {
                const std::filesystem::path file{audio->file};
                files.insert(file.is_absolute() ? file : folder / file);
            }
        }
    }
    std::vector<std::string> written;
    for (const std::filesystem::path& file : files) {
        loadPeaks(file);
        if (std::error_code error; std::filesystem::exists(peakFile(file), error)) {
            written.push_back(peakFile(file).string());
        }
    }
    return written;
}

const PeakLevels* PeakCache::levels(const std::filesystem::path& file) {
    const std::scoped_lock lock{mutex_};
    if (const auto found = files_.find(file); found != files_.end()) {
        return found->second.get();
    }
    const auto entry = files_.emplace(file, nullptr).first;
    pool_.start([this, entry] {
        auto loaded = std::make_unique<PeakLevels>(loadPeaks(entry->first));
        {
            const std::scoped_lock done{mutex_};
            entry->second = std::move(loaded);
        }
        QMetaObject::invokeMethod(this, &PeakCache::loaded, Qt::QueuedConnection);
    });
    return nullptr;
}

// We grow a take's levels in place, which is safe because the views read them on this thread.
void PeakCache::addLive(const std::filesystem::path& file, const Peaks& finest) {
    {
        const std::scoped_lock lock{mutex_};
        std::unique_ptr<PeakLevels>& levels = files_[file];
        if (live_.insert(file).second || !levels) {
            levels = std::make_unique<PeakLevels>();
        }
        Peaks& peaks = levels->levels.front();
        const std::size_t changed = peaks.minimum.size();
        peaks.minimum.insert(peaks.minimum.end(), finest.minimum.begin(), finest.minimum.end());
        peaks.maximum.insert(peaks.maximum.end(), finest.maximum.begin(), finest.maximum.end());
        foldLevels(*levels, changed);
    }
    emit loaded();
}

void PeakCache::forgetLive(const std::filesystem::path& file) {
    const std::scoped_lock lock{mutex_};
    if (live_.erase(file) > 0) {
        files_.erase(file);
    }
}

}  // namespace emerick::app
