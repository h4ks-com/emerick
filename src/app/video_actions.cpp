#include <QCoreApplication>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <system_error>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/app/stem_export.hpp"
#include "emerick/app/timeline_video.hpp"
#include "emerick/app/variant.hpp"
#include "emerick/app/video_render.hpp"

namespace emerick::app {

namespace {

constexpr int kMaxVideoWidth = 3840;
constexpr int kMaxVideoHeight = 2160;
constexpr int kMinVideoSide = 128;
constexpr double kMaxFps = 60;
constexpr const char* kVideoRenderAction = "video.render";
constexpr int kDrawMs = 50;

int videoSide(const Json& params, const std::string& key, int fallback, int maximum) {
    const int side = optionalParam<int>(params, key).value_or(fallback);
    if (side < kMinVideoSide || side > maximum || side % 2 != 0) {
        throw AppError(std::format("give {} as an even number of pixels from {} to {}", key, kMinVideoSide, maximum));
    }
    return side;
}

}  // namespace

// The checked request of a video.render call and the mix it plays, in a temporary folder that lives as long as it.
struct VideoJob {
    VideoRequest request;
    std::filesystem::path ffmpeg;
    QTemporaryDir folder;
    std::filesystem::path mix;
    std::unique_ptr<VideoWriter> writer;
};

namespace {

// Checks the params and renders the mix the video plays.
std::unique_ptr<VideoJob> prepareVideo(const Registry& registry, ProjectController& controller, const Json& params) {
    const std::filesystem::path file = registry.checkedPath(param<std::string>(params, "path"), PathAccess::Write);
    auto job = std::make_unique<VideoJob>();
    job->ffmpeg = findFfmpeg();
    const model::Document document = liveDocument(controller);
    const engine::Session session = toSession(document, registry, controller.folder());
    const core::TempoMap& tempo = document.tempo;
    const core::Ticks end = exportEnd(session, document);
    if (end <= 0) {
        throw AppError("the project has no clips to render, add a clip first");
    }
    const std::string view = optionalParam<std::string>(params, "view").value_or("fit");
    if (view != "fit" && view != "current") {
        throw AppError("give view as fit or current, not " + view);
    }
    job->request = {.file = file,
                    .width = videoSide(params, "width", VideoRequest{}.width, kMaxVideoWidth),
                    .height = videoSide(params, "height", VideoRequest{}.height, kMaxVideoHeight),
                    .fps = optionalParam<double>(params, "fps").value_or(VideoRequest{}.fps),
                    .from = optionalPosition(params, "from", tempo).value_or(0),
                    .to = std::min(optionalPosition(params, "to", tempo).value_or(end), end),
                    .currentView = view == "current"};
    const VideoRequest& request = job->request;
    if (request.fps < 1 || request.fps > kMaxFps) {
        throw AppError(std::format("give fps from 1 to {}", kMaxFps));
    }
    if (request.from < 0 || request.from >= request.to) {
        throw AppError(std::format("give from before to, both from {} to the project end {}",
                                   tempo.toBarBeatTick(0).format(), tempo.toBarBeatTick(end).format()));
    }
    if (!job->folder.isValid()) {
        throw AppError("cannot render: no temporary folder: " + job->folder.errorString().toStdString());
    }
    job->mix = std::filesystem::path{job->folder.path().toStdString()} / "mix.wav";
    exportMix(session, registry, playbackContext(controller, document), job->mix, controller.sampleRate(),
              loudnessTarget(registry, params));
    return job;
}

Json videoResult(const VideoRequest& request, int frames, const core::TempoMap& tempo) {
    return {{"file", request.file.string()},
            {"frames", frames},
            {"seconds", rounded(tempo.ticksToSeconds(request.to) - tempo.ticksToSeconds(request.from), 0.001)},
            {"width", request.width},
            {"height", request.height},
            {"fps", request.fps},
            {"from", positionState(request.from, tempo)},
            {"to", positionState(request.to, tempo)}};
}

Json renderVideo(const Registry& registry, ProjectController& controller, Nav& nav, ViewEngine& views,
                 const Json& params) {
    const std::unique_ptr<VideoJob> job = prepareVideo(registry, controller, params);
    const int frames = renderTimelineVideo(views, nav, controller, job->request, job->mix, job->ffmpeg);
    return videoResult(job->request, frames, controller.project().document().tempo);
}

}  // namespace

VideoRender::VideoRender(Registry& registry, ProjectController& controller, Nav& nav, ViewEngine& views,
                         QObject* parent)
    : QObject(parent), registry_(registry), controller_(controller), nav_(nav), views_(views) {
    frames_.setInterval(0);
    connect(&frames_, &QTimer::timeout, this, &VideoRender::step);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &VideoRender::cancel);
    // The frames draw the live project under the mix rendered at the start, so an edit or another project ends the
    // render. The controller dies before the Nav that owns us, so we never remove the listener.
    controller_.project().addListener([this](const model::Change&) {
        QMetaObject::invokeMethod(this, &VideoRender::cancelForChange, Qt::QueuedConnection);
    });
    connect(&controller_, &ProjectController::projectReplaced, this, &VideoRender::cancelForChange);
}

void VideoRender::cancelForChange() {
    if (cancel()) {
        notifyFailure("The project changed during the render, so render it again.");
    }
}

VideoRender::~VideoRender() = default;

double VideoRender::progress() const {
    if (job_ == nullptr || job_->writer->count() == 0) {
        return 0;
    }
    return static_cast<double>(job_->writer->written()) / job_->writer->count();
}

void VideoRender::render(const Json& params) {
    if (running()) {
        return;
    }
    starting_ = true;
    params_ = params;
    emit runningChanged();
    // The mix renders on this thread, so we wait for the window to draw the progress first.
    QTimer::singleShot(kDrawMs, this, &VideoRender::start);
}

void VideoRender::start() {
    if (!starting_) {
        return;
    }
    try {
        std::unique_ptr<VideoJob> job = prepareVideo(registry_, controller_, params_);
        job->writer = std::make_unique<VideoWriter>(views_, nav_, controller_, job->request, job->mix, job->ffmpeg);
        job_ = std::move(job);
        frames_.start();
    } catch (const AppError& error) {
        notifyFailure(error.what());
    } catch (const std::filesystem::filesystem_error& error) {
        notifyFailure(error.what());
    } catch (const Json::exception& error) {
        notifyFailure(error.what());
    }
    starting_ = false;
    emit runningChanged();
    emit progressChanged();
}

void VideoRender::step() {
    try {
        if (job_->writer->writeNext()) {
            emit progressChanged();
            return;
        }
        stop();
    } catch (const AppError& error) {
        cancel();
        notifyFailure(error.what());
    }
}

void VideoRender::notifyFailure(const char* what) const {
    emit registry_.noticeRaised(qs(registry_.action(kVideoRenderAction)->title), qs(what), {});
}

bool VideoRender::cancel() {
    if (starting_) {
        starting_ = false;
        emit runningChanged();
        return true;
    }
    if (job_ == nullptr) {
        return false;
    }
    const std::filesystem::path file = job_->request.file;
    stop();
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
    return true;
}

void VideoRender::stop() {
    frames_.stop();
    job_.reset();
    emit runningChanged();
    emit progressChanged();
}

void registerVideoActions(Registry& registry, ProjectController& controller, Nav& nav, ViewEngine& views) {
    // Nav is a QObject that lives as long as the views, so it owns the render.
    auto* video = std::make_unique<VideoRender>(registry, controller, nav, views, &nav).release();
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Video", video);
    registry.addAction(
        {.id = kVideoRenderAction,
         .title = "Render video",
         .description = std::format(
             "Render an MP4 (H.264 and AAC) of the tracks area under the transport counter, with the playhead "
             "walking in from the left and then holding while the clips scroll under it, and the mix as its sound, "
             "to path. width and height default to 1920 and 1080 (1280 and 720 also suit), fps to 30; from and to "
             "take ticks or bar.beat.tick and default to the project start and end. The frames show the timeline as "
             "the view has it, with the automation lanes on show; view fit, the default, shows {} bars and fits the "
             "tracks to the height, and view current keeps the view's zoom and track height. Needs ffmpeg beside the "
             "app or on PATH.",
             kVideoBarsVisible),
         .aliases = {"export video", "timeline video", "render movie"},
         .keywords = {"mp4", "movie", "h264", "share", "post"},
         .category = "Export",
         .schema = {{"required", {"path"}},
                    {"properties",
                     {{"path", {{"type", "string"}}},
                      {"width", {{"type", "integer"}, {"minimum", kMinVideoSide}, {"maximum", kMaxVideoWidth}}},
                      {"height", {{"type", "integer"}, {"minimum", kMinVideoSide}, {"maximum", kMaxVideoHeight}}},
                      {"fps", {{"type", "number"}, {"minimum", 1}, {"maximum", kMaxFps}}},
                      {"from", Json::object()},
                      {"to", Json::object()},
                      {"view", {{"type", "string"}, {"enum", {"fit", "current"}}}}}}},
         .run =
             [&registry, &controller, &nav, &views, video](const Json& params) {
                 if (registry.fromUi()) {
                     video->render(params);
                     return Json{{"started", true}};
                 }
                 return renderVideo(registry, controller, nav, views, params);
             },
         .menus = {kFileMenu},
         .dialog = FileDialog{
             .param = "path", .mode = FileDialogMode::Save, .nameFilter = "Videos (*.mp4)", .suffix = "mp4"}});
    registry.addAction({.id = "video.cancel",
                        .title = "Cancel video render",
                        .description = "Stops the video render the window runs and deletes its unfinished file.",
                        .aliases = {"stop video render"},
                        .category = "Export",
                        .enabled = [video] { return video->running(); },
                        .run = [video](const Json&) { return Json{{"cancelled", video->cancel()}}; }});
}

}  // namespace emerick::app
