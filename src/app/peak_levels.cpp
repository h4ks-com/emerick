#include "emerick/app/peak_levels.hpp"

#include <QDataStream>
#include <QList>
#include <algorithm>
#include <span>
#include <utility>

namespace emerick::app {

namespace {

constexpr quint32 kPeakFileMagic = 0x454d504b;
constexpr quint32 kPeakFileVersion = 1;
constexpr core::Frames kFold = 4;

// libc++ has no std::views::chunk yet, so we step through the groups by hand.
template <typename Pick>
void foldValues(const std::vector<float>& finer, std::vector<float>& coarser, std::size_t from, Pick pick) {
    const auto group = static_cast<std::size_t>(kFold);
    coarser.resize(from);
    for (std::size_t at = from * group; at < finer.size(); at += group) {
        coarser.push_back(pick(std::span{finer}.subspan(at, std::min(group, finer.size() - at))));
    }
}

}  // namespace

void foldLevels(PeakLevels& peaks, std::size_t changed) {
    for (std::size_t level = 1; level < peaks.levels.size(); ++level) {
        const Peaks& finer = peaks.levels.at(level - 1);
        Peaks& coarser = peaks.levels.at(level);
        changed /= static_cast<std::size_t>(kFold);
        foldValues(finer.minimum, coarser.minimum, changed, std::ranges::min);
        foldValues(finer.maximum, coarser.maximum, changed, std::ranges::max);
    }
}

PeakLevels buildLevels(Peaks finest) {
    PeakLevels peaks;
    peaks.levels.front() = std::move(finest);
    foldLevels(peaks, 0);
    return peaks;
}

std::filesystem::path peakFile(const std::filesystem::path& audio) {
    return audio.parent_path() / ".peaks" / (audio.filename().string() + ".peaks");
}

std::optional<PeakFile> readPeakFile(QIODevice& input) {
    QDataStream stream{&input};
    quint32 magic = 0;
    quint32 version = 0;
    PeakFile file;
    QList<float> minimum;
    QList<float> maximum;
    stream >> magic >> version >> file.stamp.size >> file.stamp.modified;
    if (magic != kPeakFileMagic || version != kPeakFileVersion) {
        return std::nullopt;
    }
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream >> minimum >> maximum;
    if (stream.status() != QDataStream::Ok || minimum.size() != maximum.size()) {
        return std::nullopt;
    }
    file.peaks = Peaks{{minimum.begin(), minimum.end()}, {maximum.begin(), maximum.end()}};
    return file;
}

void writePeakFile(QIODevice& output, const PeakFile& file) {
    QDataStream stream{&output};
    stream << kPeakFileMagic << kPeakFileVersion << file.stamp.size << file.stamp.modified;
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << QList<float>{file.peaks.minimum.begin(), file.peaks.minimum.end()}
           << QList<float>{file.peaks.maximum.begin(), file.peaks.maximum.end()};
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

}  // namespace emerick::app
