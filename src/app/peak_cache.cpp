#include "emerick/app/peak_cache.hpp"

#include <sndfile.h>

#include <QDataStream>
#include <QFile>
#include <QList>
#include <QSaveFile>
#include <algorithm>
#include <span>
#include <system_error>
#include <utility>

namespace emerick::app {

namespace {

constexpr quint32 kPeakFileMagic = 0x454d504b;
constexpr quint32 kPeakFileVersion = 1;
constexpr core::Frames kFold = 4;
constexpr core::Frames kBlocksPerRead = 64;

struct SndFileCloser {
    void operator()(SNDFILE* file) const { sf_close(file); }
};
using SndFile = std::unique_ptr<SNDFILE, SndFileCloser>;

// What identifies the audio a peak file was made from.
struct AudioStamp {
    quint64 size = 0;
    qint64 modified = 0;
    bool operator==(const AudioStamp&) const = default;
};

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
    const SndFile sound{sf_open(audio.c_str(), SFM_READ, &info)};
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

// libc++ has no std::views::chunk yet, so we step through the groups by hand.
template <typename Pick>
std::vector<float> foldValues(const std::vector<float>& finer, Pick pick) {
    const auto group = static_cast<std::size_t>(kFold);
    std::vector<float> coarser;
    coarser.reserve((finer.size() + group - 1) / group);
    for (std::size_t at = 0; at < finer.size(); at += group) {
        coarser.push_back(pick(std::span{finer}.subspan(at, std::min(group, finer.size() - at))));
    }
    return coarser;
}

Peaks fold(const Peaks& finer) {
    return {foldValues(finer.minimum, std::ranges::min), foldValues(finer.maximum, std::ranges::max)};
}

std::optional<Peaks> readPeakFile(const std::filesystem::path& file, const AudioStamp& stamp) {
    QFile input{QString::fromStdString(file.string())};
    if (!input.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    QDataStream stream{&input};
    quint32 magic = 0;
    quint32 version = 0;
    AudioStamp stored;
    QList<float> minimum;
    QList<float> maximum;
    stream >> magic >> version >> stored.size >> stored.modified;
    if (magic != kPeakFileMagic || version != kPeakFileVersion || stored != stamp) {
        return std::nullopt;
    }
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream >> minimum >> maximum;
    if (stream.status() != QDataStream::Ok || minimum.size() != maximum.size()) {
        return std::nullopt;
    }
    return Peaks{{minimum.begin(), minimum.end()}, {maximum.begin(), maximum.end()}};
}

// We drop the peak file when the folder is read-only, because the peaks still work from memory.
void writePeakFile(const std::filesystem::path& file, const AudioStamp& stamp, const Peaks& peaks) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    QSaveFile output{QString::fromStdString(file.string())};
    if (error || !output.open(QIODevice::WriteOnly)) {
        return;
    }
    QDataStream stream{&output};
    stream << kPeakFileMagic << kPeakFileVersion << stamp.size << stamp.modified;
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << QList<float>{peaks.minimum.begin(), peaks.minimum.end()}
           << QList<float>{peaks.maximum.begin(), peaks.maximum.end()};
    output.commit();
}

}  // namespace

PeakLevels buildLevels(Peaks finest) {
    PeakLevels peaks;
    peaks.levels.front() = std::move(finest);
    for (std::size_t level = 1; level < peaks.levels.size(); ++level) {
        peaks.levels.at(level) = fold(peaks.levels.at(level - 1));
    }
    return peaks;
}

std::filesystem::path peakFile(const std::filesystem::path& audio) {
    return audio.parent_path() / ".peaks" / (audio.filename().string() + ".peaks");
}

PeakLevels loadPeaks(const std::filesystem::path& audio) {
    const auto stamp = stampOf(audio);
    if (!stamp) {
        return {};
    }
    if (auto stored = readPeakFile(peakFile(audio), *stamp)) {
        return buildLevels(*std::move(stored));
    }
    Peaks finest = readAudioPeaks(audio);
    writePeakFile(peakFile(audio), *stamp, finest);
    return buildLevels(std::move(finest));
}

std::optional<PeakRange> peakRange(const PeakLevels& peaks, core::Frames from, core::Frames to) {
    std::size_t level = 0;
    while (level + 1 < kFramesPerPeak.size() && kFramesPerPeak.at(level + 1) <= to - from) {
        ++level;
    }
    const Peaks& chosen = peaks.levels.at(level);
    const core::Frames size = kFramesPerPeak.at(level);
    const auto count = static_cast<core::Frames>(chosen.minimum.size());
    const core::Frames first = std::max<core::Frames>(from / size, 0);
    const core::Frames last = std::min(std::max(first + 1, (to + size - 1) / size), count);
    if (first >= last) {
        return std::nullopt;
    }
    const auto span = [&](const std::vector<float>& values) {
        return std::span{values}.subspan(static_cast<std::size_t>(first), static_cast<std::size_t>(last - first));
    };
    return PeakRange{std::ranges::min(span(chosen.minimum)), std::ranges::max(span(chosen.maximum))};
}

const PeakLevels* PeakCache::levels(const std::filesystem::path& file) {
    const std::scoped_lock lock{mutex_};
    if (const auto found = files_.find(file); found != files_.end()) {
        return found->second.get();
    }
    const auto entry = files_.emplace(file, nullptr).first;
    pool_.start([this, entry] {
        auto loaded = std::make_unique<const PeakLevels>(loadPeaks(entry->first));
        {
            const std::scoped_lock done{mutex_};
            entry->second = std::move(loaded);
        }
        QMetaObject::invokeMethod(this, &PeakCache::loaded, Qt::QueuedConnection);
    });
    return nullptr;
}

// We rebuild every level on each call, so a call costs the take's whole length; folding only the new tail would not.
void PeakCache::addLive(const std::filesystem::path& file, const Peaks& finest) {
    {
        const std::scoped_lock lock{mutex_};
        Peaks& peaks = live_[file];
        peaks.minimum.insert(peaks.minimum.end(), finest.minimum.begin(), finest.minimum.end());
        peaks.maximum.insert(peaks.maximum.end(), finest.maximum.begin(), finest.maximum.end());
        files_[file] = std::make_unique<const PeakLevels>(buildLevels(peaks));
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
