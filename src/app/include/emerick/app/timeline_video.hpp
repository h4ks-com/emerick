#pragma once

#include <QImage>
#include <filesystem>
#include <memory>

#include "emerick/app/nav.hpp"
#include "emerick/app/offscreen_view.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/view_engine.hpp"
#include "emerick/core/time.hpp"

namespace emerick::app {

// The share of the lanes left of the playhead once it stops walking in from the left and the clips scroll under it.
inline constexpr double kVideoPlayheadLead = 0.3;
inline constexpr int kVideoBarsVisible = 8;
// A frame draws the views as a 1280x720 window does, scaled to the frame height.
inline constexpr double kVideoBaseHeight = 720;

// Frame n shows the position n / fps seconds after from, so the picture follows the tempo map as the audio does.
int videoFrameCount(const core::TempoMap& tempo, core::Ticks from, core::Ticks to, double fps);
core::Ticks videoFrameTicks(const core::TempoMap& tempo, core::Ticks from, int frame, double fps);
// The left edge of lanes visibleTicks wide, which holds the playhead at kVideoPlayheadLead once it gets there.
double videoScrollTicks(core::Ticks playhead, core::Ticks from, double visibleTicks);

struct VideoRequest {
    std::filesystem::path file;
    int width = 1920;
    int height = 1080;
    double fps = 30;
    core::Ticks from = 0;
    core::Ticks to = 0;
    // Keeps the zoom and track height the view has, where by default the frame shows kVideoBarsVisible bars and fits
    // the tracks to its height.
    bool currentView = false;
};

// The frames of a request: VideoFrame drawn offscreen at each frame's position. The views share Nav
// and the playhead with the app's window, so it takes them over while it lives and gives them back after.
class TimelineFrames {
public:
    TimelineFrames(ViewEngine& views, Nav& nav, ProjectController& controller, const VideoRequest& request);
    ~TimelineFrames();
    TimelineFrames(const TimelineFrames&) = delete;
    TimelineFrames& operator=(const TimelineFrames&) = delete;
    TimelineFrames(TimelineFrames&&) = delete;
    TimelineFrames& operator=(TimelineFrames&&) = delete;

    [[nodiscard]] int count() const { return count_; }
    // The frame in RGB32, request.width by request.height.
    QImage frame(int index);

private:
    struct SavedView {
        double scrollTicks = 0;
        double pixelsPerTick = 0;
        double laneWidth = 0;
        int trackHeight = 0;
        QStringList tracks;
        QString track;
        QStringList clips;
        QStringList points;
        QVariantMap blocks;
        QString marker;
    };

    Nav& nav_;
    ProjectController& controller_;
    VideoRequest request_;
    OffscreenView view_;
    SavedView saved_;
    int count_ = 0;
    double visibleTicks_ = 0;
    std::unique_ptr<QObject> root_;
};

// The ffmpeg beside the app, else on PATH or in Homebrew's folders. Throws AppError when there is none.
std::filesystem::path findFfmpeg();

// Encodes every frame of the request with mix, a WAV of the project from tick 0, into an H.264 and AAC MP4 at
// request.file. Returns the frame count.
int renderTimelineVideo(ViewEngine& views, Nav& nav, ProjectController& controller, const VideoRequest& request,
                        const std::filesystem::path& mix, const std::filesystem::path& ffmpeg);

}  // namespace emerick::app
