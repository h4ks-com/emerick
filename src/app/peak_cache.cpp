#include "emerick/app/peak_cache.hpp"

#include <sndfile.h>

#include <QByteArray>
#include <QPainter>
#include <QStringList>
#include <algorithm>
#include <memory>
#include <ranges>
#include <span>

namespace emerick::app {

namespace {

constexpr QSize kDefaultSize{512, 64};

struct SndFileCloser {
    void operator()(SNDFILE* file) const { sf_close(file); }
};
using SndFile = std::unique_ptr<SNDFILE, SndFileCloser>;

Peaks readPeaks(const std::filesystem::path& file) {
    SF_INFO info{};
    const SndFile sound{sf_open(file.c_str(), SFM_READ, &info)};
    if (!sound || info.channels <= 0) {
        return {};
    }
    const auto channels = static_cast<std::size_t>(info.channels);
    std::vector<float> block(static_cast<std::size_t>(kFramesPerPeak) * channels);
    Peaks peaks;
    while (true) {
        const sf_count_t read = sf_readf_float(sound.get(), block.data(), kFramesPerPeak);
        if (read <= 0) {
            break;
        }
        const auto samples = block | std::views::take(static_cast<std::size_t>(read) * channels);
        const auto [low, high] = std::ranges::minmax(samples);
        peaks.minimum.push_back(low);
        peaks.maximum.push_back(high);
    }
    return peaks;
}

}  // namespace

const Peaks& PeakCache::peaks(const std::filesystem::path& file) {
    const std::scoped_lock lock{mutex_};
    if (const auto found = files_.find(file); found != files_.end()) {
        return found->second;
    }
    return files_.emplace(file, readPeaks(file)).first->second;
}

QImage drawPeaks(const Peaks& peaks, core::Frames offset, core::Frames frames, QSize size) {
    QImage image{size, QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::transparent);
    const auto count = static_cast<core::Frames>(peaks.minimum.size());
    if (frames <= 0 || count == 0) {
        return image;
    }
    QPainter painter{&image};
    const double middle = size.height() / 2.0;
    for (int x = 0; x < size.width(); ++x) {
        const core::Frames from = offset + frames * x / size.width();
        const core::Frames to = offset + frames * (x + 1) / size.width();
        const core::Frames first = std::max<core::Frames>(from / kFramesPerPeak, 0);
        const core::Frames last = std::min(std::max(first + 1, (to + kFramesPerPeak - 1) / kFramesPerPeak), count);
        if (first >= last) {
            continue;
        }
        const auto span = [&](const std::vector<float>& values) {
            return std::span{values}.subspan(static_cast<std::size_t>(first), static_cast<std::size_t>(last - first));
        };
        const float low = std::ranges::min(span(peaks.minimum));
        const float high = std::ranges::max(span(peaks.maximum));
        const double top = middle - std::clamp(high, -1.0F, 1.0F) * middle;
        const double bottom = middle - std::clamp(low, -1.0F, 1.0F) * middle;
        painter.fillRect(QRectF{static_cast<double>(x), top, 1.0, std::max(1.0, bottom - top)}, Qt::black);
    }
    return image;
}

QString peakSource(const std::filesystem::path& file, core::Frames offset, core::Frames frames) {
    return QStringLiteral("image://peaks/%1/%2/%3")
        .arg(offset)
        .arg(frames)
        .arg(QString::fromLatin1(QByteArray::fromStdString(file.string()).toHex()));
}

QImage PeakImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    const QStringList parts = id.split('/');
    const QSize target{requestedSize.width() > 0 ? requestedSize.width() : kDefaultSize.width(),
                       requestedSize.height() > 0 ? requestedSize.height() : kDefaultSize.height()};
    if (size != nullptr) {
        *size = target;
    }
    if (parts.size() != 3) {
        return drawPeaks({}, 0, 0, target);
    }
    const std::filesystem::path file{QByteArray::fromHex(parts[2].toLatin1()).toStdString()};
    return drawPeaks(cache_.peaks(file), parts[0].toLongLong(), parts[1].toLongLong(), target);
}

}  // namespace emerick::app
