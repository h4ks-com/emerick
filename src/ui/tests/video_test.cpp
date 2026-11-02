#include <QColor>
#include <QProcess>
#include <QStandardPaths>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "emerick/app/timeline_video.hpp"
#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr double kFps = 30;
const QColor kTape{0xF2, 0xB5, 0x44};

// One audio track holding a clip of that many seconds at bar 1.
void addClip(MainWindow& main, double seconds) {
    const std::string track = main.rig.run("track.addAudio").at("id");
    const std::filesystem::path file = main.rig.path("tone.wav");
    writeConstant(file, static_cast<core::Frames>(seconds * kRate), 0.5F);
    main.rig.run("clip.import", {{"track", track}, {"path", file.string()}});
    MainWindow::settle();
}

// The mean x of the playhead's color along row y.
std::optional<double> playheadColumn(const QImage& image, int y) {
    constexpr int kTolerance = 24;
    std::vector<int> columns;
    for (int x = 0; x < image.width(); ++x) {
        const QColor pixel = image.pixelColor(x, y);
        if (std::abs(pixel.red() - kTape.red()) + std::abs(pixel.green() - kTape.green()) +
                std::abs(pixel.blue() - kTape.blue()) <
            kTolerance) {
            columns.push_back(x);
        }
    }
    if (columns.empty()) {
        return std::nullopt;
    }
    double sum = 0;
    for (const int x : columns) {
        sum += x;
    }
    return sum / static_cast<double>(columns.size());
}

}  // namespace

TEST_CASE("the playhead of each frame sits on that frame's tick and then holds while the lanes scroll") {
    MainWindow& main = MainWindow::instance();
    addClip(main, 20);
    const double scrollBefore = main.rig.nav.scrollTicks();
    const core::TempoMap tempo = main.rig.controller->project().document().tempo;
    const double headerWidth = main.rig.registry.settingValue("view.headerWidth").get<double>();
    const double scale = kHeight / kVideoBaseHeight;
    {
        TimelineFrames frames(
            *main.rig.views, main.rig.nav, *main.rig.controller,
            {.width = kWidth, .height = kHeight, .fps = kFps, .from = 0, .to = tempo.ticksPerBar() * 8});
        const double laneWidth = kWidth / scale - headerWidth;
        for (const int index : {0, 10, 45, 120, 300}) {
            const QImage image = frames.frame(index);
            REQUIRE(image.size() == QSize{kWidth, kHeight});
            const core::Ticks tick = videoFrameTicks(tempo, 0, index, kFps);
            CHECK(main.rig.controller->positionTicks() == tick);
            const double expected = scale * (headerWidth + ((static_cast<double>(tick) - main.rig.nav.scrollTicks()) *
                                                            main.rig.nav.pixelsPerTick()));
            const std::optional<double> column = playheadColumn(image, kHeight * 3 / 4);
            INFO("frame " << index);
            REQUIRE(column.has_value());
            CHECK(*column == Catch::Approx(expected).margin(1.5));
            if (main.rig.nav.scrollTicks() > 0) {
                CHECK(*column == Catch::Approx(scale * (headerWidth + (laneWidth * kVideoPlayheadLead))).margin(1.5));
            }
        }
    }
    CHECK(main.rig.nav.scrollTicks() == scrollBefore);
    CHECK(main.rig.controller->positionTicks() == 0);
}

TEST_CASE("a rendered video holds a frame per period and the mix for as long as the picture lasts") {
    const QString ffprobe = QStandardPaths::findExecutable("ffprobe");
    try {
        findFfmpeg();
    } catch (const AppError&) {
        SKIP("ffmpeg is not installed");
    }
    if (ffprobe.isEmpty()) {
        SKIP("ffprobe is not installed");
    }
    MainWindow& main = MainWindow::instance();
    if (main.rig.controller->project().document().tracks.empty()) {
        addClip(main, 20);
    }
    const std::filesystem::path file = main.rig.path("song.mp4");
    const Json result = main.rig.run(
        "video.render",
        {{"path", file.string()}, {"width", 640}, {"height", 360}, {"fps", 10}, {"from", "2.1.1"}, {"to", "3.1.1"}});
    CHECK(result.at("frames") == 20);

    QProcess probe;
    probe.start(ffprobe, {"-v", "error", "-count_frames", "-show_entries",
                          "stream=codec_type,codec_name,nb_read_frames,duration", "-of", "json",
                          QString::fromStdString(file.string())});
    REQUIRE(probe.waitForFinished());
    const Json streams = Json::parse(probe.readAllStandardOutput().toStdString()).at("streams");
    std::optional<Json> video;
    std::optional<Json> audio;
    for (const Json& stream : streams) {
        (stream.at("codec_type") == "video" ? video : audio) = stream;
    }
    REQUIRE(video.has_value());
    REQUIRE(audio.has_value());
    CHECK(video->at("codec_name") == "h264");
    CHECK(audio->at("codec_name") == "aac");
    CHECK(video->at("nb_read_frames") == "20");
    const double videoSeconds = std::stod(video->at("duration").get<std::string>());
    const double audioSeconds = std::stod(audio->at("duration").get<std::string>());
    CHECK(videoSeconds == Catch::Approx(2.0).margin(0.1));
    CHECK(audioSeconds == Catch::Approx(videoSeconds).margin(0.1));
}
