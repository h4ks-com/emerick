#include "emerick/app/peak_cache.hpp"

#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <thread>
#include <vector>

#include "emerick/app/audio_file.hpp"
#include "qt_app.hpp"

using namespace emerick;
using namespace emerick::app;

namespace {

constexpr double kRate = 48000.0;
constexpr core::Frames kHalf = 4 * kFramesPerPeak;

// The first half swings between -0.5 and 0.75 on the left channel, the second half is silent.
std::filesystem::path writeTestFile(const QTemporaryDir& dir) {
    std::vector<float> left(2 * kHalf, 0.0F);
    std::vector<float> right(2 * kHalf, 0.0F);
    for (core::Frames frame = 0; frame < kHalf; ++frame) {
        left[static_cast<std::size_t>(frame)] = frame % 2 == 0 ? 0.75F : -0.5F;
    }
    const std::filesystem::path path = dir.filePath("swing.wav").toStdString();
    writeWav(path, {left, right}, kRate);
    return path;
}

bool columnHasInk(const QImage& image, int x, int y) { return qAlpha(image.pixel(x, y)) > 0; }

}  // namespace

TEST_CASE("peaks hold the lowest and highest sample of every block over all channels") {
    const QTemporaryDir dir;
    PeakCache cache;
    const Peaks& peaks = cache.peaks(writeTestFile(dir));
    REQUIRE(peaks.maximum.size() == 8);
    CHECK(peaks.maximum.front() == 0.75F);
    CHECK(peaks.minimum.front() == -0.5F);
    CHECK(peaks.maximum.back() == 0.0F);
    CHECK(peaks.minimum.back() == 0.0F);
}

TEST_CASE("the cache reads a file once and shares it across threads") {
    const QTemporaryDir dir;
    const auto path = writeTestFile(dir);
    PeakCache cache;
    const Peaks* first = &cache.peaks(path);
    std::vector<const Peaks*> seen(4, nullptr);
    std::vector<std::jthread> threads;
    for (auto& slot : seen) {
        threads.emplace_back([&cache, &path, &slot] { slot = &cache.peaks(path); });
    }
    threads.clear();
    for (const Peaks* peaks : seen) {
        CHECK(peaks == first);
    }
    CHECK(cache.peaks(dir.filePath("missing.wav").toStdString()).maximum.empty());
}

TEST_CASE("drawing peaks inks the loud half tall and the silent half as a center line") {
    const QTemporaryDir dir;
    PeakCache cache;
    const QImage image = drawPeaks(cache.peaks(writeTestFile(dir)), 0, 2 * kHalf, QSize{100, 40});
    CHECK(image.size() == QSize{100, 40});
    CHECK(columnHasInk(image, 10, 8));
    CHECK(columnHasInk(image, 10, 28));
    CHECK_FALSE(columnHasInk(image, 10, 2));
    CHECK(columnHasInk(image, 90, 20));
    CHECK_FALSE(columnHasInk(image, 90, 8));
}

TEST_CASE("the image provider serves the stretch named in its source") {
    ensureApplication();
    const QTemporaryDir dir;
    const auto path = writeTestFile(dir);
    PeakCache cache;
    PeakImageProvider provider{cache};
    const QString source = peakSource(path, kHalf, kHalf);
    REQUIRE(source.startsWith("image://peaks/"));
    QSize size;
    const QImage silent = provider.requestImage(source.mid(14), &size, QSize{50, 20});
    CHECK(size == QSize{50, 20});
    CHECK_FALSE(columnHasInk(silent, 25, 2));
    CHECK(columnHasInk(silent, 25, 10));
    const QImage loud = provider.requestImage(peakSource(path, 0, kHalf).mid(14), &size, QSize{});
    CHECK(size.width() > 0);
    CHECK(columnHasInk(loud, 5, 8));
}
