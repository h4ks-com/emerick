#include "emerick/app/peak_cache.hpp"

#include <QTemporaryDir>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <span>
#include <vector>

#include "emerick/app/audio_file.hpp"
#include "qt_app.hpp"

using namespace emerick;
using namespace emerick::app;

namespace {

constexpr double kRate = 48000.0;
// Three coarsest blocks and a partial one, so every level ends on a short block.
constexpr core::Frames kFrames = 3 * kFramesPerPeak.back() + 700;

struct TestFile {
    std::filesystem::path path;
    std::vector<float> left;
    std::vector<float> right;
};

TestFile writeTestFile(const QTemporaryDir& dir) {
    TestFile file{dir.filePath("swing.wav").toStdString(), {}, {}};
    for (core::Frames frame = 0; frame < kFrames; ++frame) {
        const auto at = static_cast<double>(frame);
        file.left.push_back(static_cast<float>(std::sin(at * 0.001) * std::cos(at * 0.00007)));
        file.right.push_back(static_cast<float>(0.3 * std::sin(at * 0.013)));
    }
    writeWav(file.path, {file.left, file.right}, kRate);
    return file;
}

PeakRange bruteForce(const TestFile& file, core::Frames from, core::Frames to) {
    const auto first = static_cast<std::size_t>(from);
    const auto count = static_cast<std::size_t>(std::min(to, kFrames) - from);
    const auto left = std::span{file.left}.subspan(first, count);
    const auto right = std::span{file.right}.subspan(first, count);
    return {std::min(std::ranges::min(left), std::ranges::min(right)),
            std::max(std::ranges::max(left), std::ranges::max(right))};
}

}  // namespace

TEST_CASE("every peak level holds the lowest and highest sample of its blocks over all channels") {
    const QTemporaryDir dir;
    const TestFile file = writeTestFile(dir);
    const PeakLevels peaks = loadPeaks(file.path);
    for (std::size_t level = 0; level < kFramesPerPeak.size(); ++level) {
        const core::Frames size = kFramesPerPeak.at(level);
        const Peaks& values = peaks.levels.at(level);
        REQUIRE(values.minimum.size() == static_cast<std::size_t>((kFrames + size - 1) / size));
        for (std::size_t block = 0; block < values.minimum.size(); ++block) {
            const auto from = static_cast<core::Frames>(block) * size;
            const PeakRange expected = bruteForce(file, from, from + size);
            CHECK(values.minimum.at(block) == expected.low);
            CHECK(values.maximum.at(block) == expected.high);
        }
    }
}

TEST_CASE("a peak range reads the coarsest level that covers it and matches the samples of its blocks") {
    const QTemporaryDir dir;
    const TestFile file = writeTestFile(dir);
    const PeakLevels peaks = loadPeaks(file.path);
    const auto range = peakRange(peaks, 2 * kFramesPerPeak.at(2), 3 * kFramesPerPeak.at(2));
    REQUIRE(range.has_value());
    const PeakRange expected = bruteForce(file, 2 * kFramesPerPeak.at(2), 3 * kFramesPerPeak.at(2));
    CHECK(range->low == expected.low);
    CHECK(range->high == expected.high);
    CHECK_FALSE(peakRange(peaks, kFrames + kFramesPerPeak.back(), kFrames + 2 * kFramesPerPeak.back()).has_value());
}

TEST_CASE("the peak file is reused until the audio changes") {
    const QTemporaryDir dir;
    const TestFile file = writeTestFile(dir);
    const std::filesystem::path stored = peakFile(file.path);
    CHECK(stored == file.path.parent_path() / ".peaks" / "swing.wav.peaks");

    const PeakLevels first = loadPeaks(file.path);
    REQUIRE(std::filesystem::exists(stored));
    const auto written = std::filesystem::last_write_time(stored);
    const PeakLevels again = loadPeaks(file.path);
    const bool untouched = std::filesystem::last_write_time(stored) == written;
    CHECK(untouched);
    CHECK(again.levels.front().maximum == first.levels.front().maximum);

    writeWav(file.path, {std::vector<float>(static_cast<std::size_t>(kFramesPerPeak.front()), 0.25F)}, kRate);
    const PeakLevels changed = loadPeaks(file.path);
    REQUIRE(changed.levels.front().maximum.size() == 1);
    CHECK(changed.levels.front().maximum.front() == 0.25F);
    CHECK(loadPeaks(file.path).levels.front().maximum == changed.levels.front().maximum);
}

TEST_CASE("the cache loads a file off the calling thread once and keeps it") {
    ensureApplication();
    const QTemporaryDir dir;
    const TestFile file = writeTestFile(dir);
    PeakCache cache;
    int loads = 0;
    QObject::connect(&cache, &PeakCache::loaded, [&loads] { ++loads; });
    CHECK(cache.levels(file.path) == nullptr);
    REQUIRE(processUntil([&] { return loads == 1; }));
    const PeakLevels* levels = cache.levels(file.path);
    REQUIRE(levels != nullptr);
    CHECK(cache.levels(file.path) == levels);
    CHECK(levels->levels.front().maximum.size() == static_cast<std::size_t>(kFrames / kFramesPerPeak.front() + 1));

    CHECK(cache.levels(dir.filePath("missing.wav").toStdString()) == nullptr);
    REQUIRE(processUntil([&] { return loads == 2; }));
    CHECK(cache.levels(dir.filePath("missing.wav").toStdString())->levels.front().maximum.empty());
}

TEST_CASE("writing a project's peaks writes one peak file per audio file its clips play") {
    const QTemporaryDir dir;
    const TestFile file = writeTestFile(dir);
    model::Document document;
    model::Track track{.name = "Swing", .kind = "audio"};
    track.clips.emplace_back(model::AudioClip{.start = 0, .length = 3840, .file = "swing.wav"});
    track.clips.emplace_back(model::AudioClip{.start = 3840, .length = 3840, .file = "swing.wav"});
    track.clips.emplace_back(model::AudioClip{.start = 7680, .length = 3840, .file = "missing.wav"});
    document.tracks.push_back(track);

    CHECK(writePeaks(document, dir.path().toStdString()) == std::vector{peakFile(file.path).string()});
    CHECK(std::filesystem::exists(peakFile(file.path)));
}
