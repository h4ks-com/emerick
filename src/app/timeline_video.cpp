#include "emerick/app/timeline_video.hpp"

#include <QByteArrayView>
#include <QCoreApplication>
#include <QProcess>
#include <QQmlComponent>
#include <QStandardPaths>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "emerick/app/automation_params.hpp"
#include "emerick/core/overloaded.hpp"

namespace emerick::app {

namespace {

constexpr double kFrameEpsilon = 1e-9;
constexpr auto kPeaksTimeout = std::chrono::seconds{30};
constexpr auto kPeaksPoll = std::chrono::milliseconds{5};
constexpr qint64 kBufferedFrames = 2;
constexpr qint64 kBytesPerPixel = 4;

void waitForPeaks(PeakCache& peaks, const model::Document& document, const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> files;
    for (const model::Track& track : document.tracks) {
        for (const model::Clip& clip : track.clips) {
            std::visit(core::Overloaded{[&](const model::AudioClip& audio) {
                                            const std::filesystem::path file{audio.file};
                                            files.push_back(file.is_absolute() ? file : folder / file);
                                        },
                                        [](const model::MidiClip&) {}},
                       clip);
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + kPeaksTimeout;
    while (!std::ranges::all_of(files, [&](const std::filesystem::path& file) { return peaks.levels(file); })) {
        if (std::chrono::steady_clock::now() > deadline) {
            throw AppError("cannot render the video: the waveforms did not load within 30 s");
        }
        std::this_thread::sleep_for(kPeaksPoll);
    }
}

}  // namespace

// Feeds frames to ffmpeg on stdin, which muxes them with the mix into the MP4.
class VideoEncoder {
public:
    VideoEncoder(const std::filesystem::path& ffmpeg, const VideoRequest& request, const std::filesystem::path& mix,
                 double fromSeconds, double seconds)
        : frameBytes_(qint64{request.width} * request.height * kBytesPerPixel) {
        const QString whitelist = QStringLiteral("file,pipe");
        process_.setStandardOutputFile(QProcess::nullDevice());
        process_.start(qs(ffmpeg.string()), {"-nostdin",
                                             "-hide_banner",
                                             "-loglevel",
                                             "error",
                                             "-protocol_whitelist",
                                             whitelist,
                                             "-f",
                                             "rawvideo",
                                             "-pixel_format",
                                             "bgra",
                                             "-video_size",
                                             QStringLiteral("%1x%2").arg(request.width).arg(request.height),
                                             "-framerate",
                                             QString::number(request.fps),
                                             "-i",
                                             "pipe:0",
                                             "-protocol_whitelist",
                                             whitelist,
                                             "-ss",
                                             QString::number(fromSeconds, 'f', 6),
                                             "-i",
                                             qs("file:" + mix.string()),
                                             "-map",
                                             "0:v",
                                             "-map",
                                             "1:a",
                                             "-c:v",
                                             "libx264",
                                             "-preset",
                                             "veryfast",
                                             "-crf",
                                             "20",
                                             "-pix_fmt",
                                             "yuv420p",
                                             "-c:a",
                                             "aac",
                                             "-b:a",
                                             "192k",
                                             "-t",
                                             QString::number(seconds, 'f', 6),
                                             "-movflags",
                                             "+faststart",
                                             "-protocol_whitelist",
                                             whitelist,
                                             "-y",
                                             qs("file:" + request.file.string())});
        if (!process_.waitForStarted()) {
            throw AppError("cannot start " + ffmpeg.string() + ": " + process_.errorString().toStdString());
        }
    }
    ~VideoEncoder() {
        if (process_.state() != QProcess::NotRunning) {
            process_.kill();
            process_.waitForFinished(-1);
        }
    }
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;
    VideoEncoder(VideoEncoder&&) = delete;
    VideoEncoder& operator=(VideoEncoder&&) = delete;

    void write(const QImage& frame) {
        const QByteArrayView bytes{frame.constBits(), frame.sizeInBytes()};
        process_.write(bytes.data(), bytes.size());
        while (process_.bytesToWrite() > kBufferedFrames * frameBytes_) {
            if (!process_.waitForBytesWritten(-1)) {
                fail();
            }
        }
    }

    void finish() {
        process_.closeWriteChannel();
        process_.waitForFinished(-1);
        if (process_.exitStatus() != QProcess::NormalExit || process_.exitCode() != 0) {
            fail();
        }
    }

private:
    [[noreturn]] void fail() {
        process_.kill();
        process_.waitForFinished(-1);
        throw AppError("ffmpeg could not encode the video: " + process_.readAllStandardError().trimmed().toStdString());
    }

    qint64 frameBytes_;
    QProcess process_;
};

namespace {

// Each track takes a row, and while the lanes are open each further lane whose param the track can automate takes
// another, as the track list draws them.
std::size_t timelineRows(const Registry& registry, const Nav& nav, const ProjectController& controller) {
    const model::Document& document = controller.project().document();
    if (!nav.automationOpen()) {
        return document.tracks.size();
    }
    std::size_t rows = 0;
    for (const model::Track& track : document.tracks) {
        const std::vector<AutomationParam> params =
            automationParams(registry, document, track, controller.instruments().insertPlugins());
        const QStringList shown = nav.shownLanes(qs(track.id.value));
        rows += 1 + static_cast<std::size_t>(std::ranges::count_if(shown.sliced(1), [&](const QString& id) {
                    return std::ranges::contains(params, id.toStdString(), &AutomationParam::id);
                }));
    }
    return rows;
}

}  // namespace

int videoFrameCount(const core::TempoMap& tempo, core::Ticks from, core::Ticks to, double fps) {
    const double seconds = tempo.ticksToSeconds(to) - tempo.ticksToSeconds(from);
    return static_cast<int>(std::ceil((seconds * fps) - kFrameEpsilon));
}

core::Ticks videoFrameTicks(const core::TempoMap& tempo, core::Ticks from, int frame, double fps) {
    return tempo.secondsToTicks(tempo.ticksToSeconds(from) + (frame / fps));
}

double videoScrollTicks(core::Ticks playhead, core::Ticks from, double visibleTicks) {
    return std::max(static_cast<double>(from), static_cast<double>(playhead) - (visibleTicks * kVideoPlayheadLead));
}

TimelineFrames::TimelineFrames(ViewEngine& views, Nav& nav, ProjectController& controller, const VideoRequest& request)
    : nav_(nav),
      controller_(controller),
      request_(request),
      view_({request.width, request.height}),
      saved_{.scrollTicks = nav.scrollTicks(),
             .pixelsPerTick = nav.pixelsPerTick(),
             .laneWidth = nav.laneWidth(),
             .trackHeight = nav.trackHeight(),
             .tracks = nav.selectedTracks(),
             .track = nav.selectedTrack(),
             .clips = nav.selectedClips(),
             .points = nav.selectedPoints(),
             .blocks = nav.selectedBlocks(),
             .marker = nav.selectedMarker()} {
    const model::Document& document = controller.project().document();
    const core::TempoMap& tempo = document.tempo;
    waitForPeaks(views.peaks(), document, controller.folder());
    count_ = videoFrameCount(tempo, request.from, request.to, request.fps);
    QQmlComponent component(&views.engine(), "Emerick.Ui", "VideoFrame");
    root_.reset(component.create());
    auto* frame = qobject_cast<QQuickItem*>(root_.get());
    if (frame == nullptr) {
        throw AppError("cannot load the video frame: " + component.errorString().toStdString());
    }
    nav.selectTracks({}, {});
    nav.setSelectedClips({});
    nav.setSelectedPoints({});
    nav.setSelectedBlocks({});
    nav.setSelectedMarker({});
    const QSizeF size{QSize{request.width, request.height}};
    const double ratio = view_.devicePixelRatio();
    const double scale = size.height() / kVideoBaseHeight / ratio;
    frame->setParentItem(view_.contentItem());
    frame->setTransformOrigin(QQuickItem::TopLeft);
    frame->setScale(scale);
    frame->setSize(size / ratio / scale);
    const auto* tracks = frame->findChild<QQuickItem*>("video.tracks");
    const double laneWidth = tracks->property("laneWidth").toDouble();
    if (!request.currentView) {
        nav.setPixelsPerTick(laneWidth / static_cast<double>(kVideoBarsVisible * tempo.ticksPerBar()));
        if (!document.tracks.empty()) {
            nav.setTrackHeight(static_cast<int>((tracks->height() - tracks->property("lanesTop").toDouble()) /
                                                static_cast<double>(timelineRows(views.registry(), nav, controller))));
        }
    }
    visibleTicks_ = laneWidth / nav.pixelsPerTick();
}

TimelineFrames::~TimelineFrames() {
    root_.reset();
    controller_.setDisplayPosition(std::nullopt);
    nav_.setPixelsPerTick(saved_.pixelsPerTick);
    nav_.setTrackHeight(saved_.trackHeight);
    nav_.setScrollTicks(saved_.scrollTicks);
    nav_.setLaneWidth(saved_.laneWidth);
    nav_.selectTracks(saved_.tracks, saved_.track);
    nav_.setSelectedClips(saved_.clips);
    nav_.setSelectedPoints(saved_.points);
    nav_.setSelectedBlocks(saved_.blocks);
    nav_.setSelectedMarker(saved_.marker);
}

QImage TimelineFrames::frame(int index) {
    const core::Ticks playhead =
        videoFrameTicks(controller_.project().document().tempo, request_.from, index, request_.fps);
    // The window's timeline can scroll when the playhead moves, so we set our scroll after it.
    controller_.setDisplayPosition(playhead);
    nav_.setScrollTicks(videoScrollTicks(playhead, request_.from, visibleTicks_));
    return view_.grab();
}

std::filesystem::path findFfmpeg() {
    // An app opened from Finder gets no shell PATH, so we also look where Homebrew installs.
    for (const QStringList& folders : {QStringList{QCoreApplication::applicationDirPath()}, QStringList{},
                                       QStringList{"/opt/homebrew/bin", "/usr/local/bin"}}) {
        if (const QString found = QStandardPaths::findExecutable("ffmpeg", folders); !found.isEmpty()) {
            return found.toStdString();
        }
    }
    throw AppError("cannot find ffmpeg: install it (brew install ffmpeg on macOS) or put it beside the app");
}

int renderTimelineVideo(ViewEngine& views, Nav& nav, ProjectController& controller, const VideoRequest& request,
                        const std::filesystem::path& mix, const std::filesystem::path& ffmpeg) {
    VideoWriter writer(views, nav, controller, request, mix, ffmpeg);
    while (writer.writeNext()) {
    }
    return writer.count();
}

VideoWriter::VideoWriter(ViewEngine& views, Nav& nav, ProjectController& controller, const VideoRequest& request,
                         const std::filesystem::path& mix, const std::filesystem::path& ffmpeg)
    : frames_(views, nav, controller, request) {
    const core::TempoMap& tempo = controller.project().document().tempo;
    const double fromSeconds = tempo.ticksToSeconds(request.from);
    encoder_ = std::make_unique<VideoEncoder>(ffmpeg, request, mix, fromSeconds,
                                              tempo.ticksToSeconds(request.to) - fromSeconds);
}

VideoWriter::~VideoWriter() = default;

bool VideoWriter::writeNext() {
    if (written_ == frames_.count()) {
        return false;
    }
    encoder_->write(frames_.frame(written_));
    ++written_;
    if (written_ == frames_.count()) {
        encoder_->finish();
    }
    return true;
}

}  // namespace emerick::app
