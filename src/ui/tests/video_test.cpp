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

TEST_CASE("a frame draws the automation curve between the points") {
    MainWindow& main = MainWindow::instance();
    while (!main.rig.controller->project().document().tracks.empty()) {
        main.rig.run("track.remove", {{"track", main.rig.controller->project().document().tracks.front().id.value}});
    }
    addClip(main, 20);
    const std::string track = main.rig.controller->project().document().tracks.front().id.value;
    const core::Ticks bar = main.rig.controller->project().document().tempo.ticksPerBar();
    main.rig.nav.setAutomationOpen(true);
    main.rig.run("automation.addPoint", {{"track", track}, {"tick", 0}, {"value", -60.0}});
    const std::string last =
        main.rig.run("automation.addPoint", {{"track", track}, {"tick", bar * 6}, {"value", 0.0}}).at("point");
    const auto render = [&] {
        TimelineFrames frames(*main.rig.views, main.rig.nav, *main.rig.controller,
                              {.width = kWidth, .height = kHeight, .fps = kFps, .from = 0, .to = bar * 8});
        return frames.frame(0);
    };
    const QImage rising = render();
    main.rig.run("automation.movePoints", {{"points", Json::array({last})}, {"value", -60.0}});
    const QImage flat = render();
    main.rig.nav.setAutomationOpen(false);

    const double scale = kHeight / kVideoBaseHeight;
    const double headerWidth = main.rig.registry.settingValue("view.headerWidth").get<double>();
    const double laneWidth = kWidth / scale - headerWidth;
    const int middle = static_cast<int>(scale * (headerWidth + (laneWidth * 3 / kVideoBarsVisible)));
    int changed = 0;
    for (int y = 0; y < kHeight; ++y) {
        changed += rising.pixel(middle, y) != flat.pixel(middle, y) ? 1 : 0;
    }
    CHECK(changed > 0);
}

TEST_CASE("a frame fits every lane row and draws a bus's lanes on the bus rows") {
    MainWindow& main = MainWindow::instance();
    while (!main.rig.controller->project().document().tracks.empty()) {
        main.rig.run("track.remove", {{"track", main.rig.controller->project().document().tracks.front().id.value}});
    }
    addClip(main, 20);
    const std::string vox = main.rig.controller->project().document().tracks.front().id.value;
    const std::string mix = main.rig.run("track.addBus", {{"name", "Mix"}}).at("id");
    for (const std::string& track : {vox, mix}) {
        for (const char* param : {"pan", "mute"}) {
            main.rig.run("automation.showLane", {{"track", track}, {"param", param}, {"add", true}});
        }
    }
    const core::Ticks bar = main.rig.controller->project().document().tempo.ticksPerBar();
    main.rig.run("automation.addPoint", {{"track", mix}, {"tick", 0}, {"value", -60.0}});
    const std::string gain =
        main.rig.run("automation.addPoint", {{"track", mix}, {"tick", bar * 6}, {"value", 0.0}}).at("point");
    main.rig.run("automation.addPoint", {{"track", mix}, {"param", "mute"}, {"tick", 0}, {"value", 0.0}});
    const std::string mute =
        main.rig.run("automation.addPoint", {{"track", mix}, {"param", "mute"}, {"tick", bar * 6}, {"value", 1.0}})
            .at("point");
    const auto render = [&] {
        TimelineFrames frames(*main.rig.views, main.rig.nav, *main.rig.controller,
                              {.width = kWidth, .height = kHeight, .fps = kFps, .from = 0, .to = bar * 8});
        return frames.frame(0);
    };
    const QImage rising = render();
    main.rig.run("automation.movePoints", {{"points", Json::array({gain})}, {"value", -60.0}});
    const QImage flatGain = render();
    main.rig.run("automation.movePoints", {{"points", Json::array({mute})}, {"value", 0.0}});
    const QImage flatMute = render();
    main.rig.nav.setAutomationLanes({});
    main.rig.nav.setAutomationOpen(false);
    MainWindow::settle();

    const double scale = kHeight / kVideoBaseHeight;
    const double headerWidth = main.rig.registry.settingValue("view.headerWidth").get<double>();
    const double laneWidth = kWidth / scale - headerWidth;
    const int middle = static_cast<int>(scale * (headerWidth + (laneWidth * 3 / kVideoBarsVisible)));
    const auto changedRows = [&](const QImage& before, const QImage& after) {
        std::vector<int> rows;
        for (int y = 0; y < kHeight; ++y) {
            if (before.pixel(middle, y) != after.pixel(middle, y)) {
                rows.push_back(y);
            }
        }
        return rows;
    };
    const std::vector<int> gainRows = changedRows(rising, flatGain);
    const std::vector<int> muteRows = changedRows(flatGain, flatMute);
    REQUIRE_FALSE(gainRows.empty());
    REQUIRE_FALSE(muteRows.empty());
    CHECK(muteRows.front() > gainRows.back());
}

TEST_CASE("a frame keeps the view's zoom and track height when asked to") {
    MainWindow& main = MainWindow::instance();
    if (main.rig.controller->project().document().tracks.empty()) {
        addClip(main, 20);
    }
    main.rig.nav.setPixelsPerTick(0.1);
    main.rig.nav.setTrackHeight(100);
    const core::Ticks bar = main.rig.controller->project().document().tempo.ticksPerBar();
    TimelineFrames frames(
        *main.rig.views, main.rig.nav, *main.rig.controller,
        {.width = kWidth, .height = kHeight, .fps = kFps, .from = 0, .to = bar * 8, .currentView = true});
    frames.frame(0);
    CHECK(main.rig.nav.pixelsPerTick() == Catch::Approx(0.1));
    CHECK(main.rig.nav.trackHeight() == 100);
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
