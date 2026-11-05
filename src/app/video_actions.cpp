#include <QTemporaryDir>
#include <algorithm>
#include <filesystem>
#include <format>
#include <string>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/app/stem_export.hpp"
#include "emerick/app/timeline_video.hpp"

namespace emerick::app {

namespace {

constexpr int kMaxVideoWidth = 3840;
constexpr int kMaxVideoHeight = 2160;
constexpr int kMinVideoSide = 128;
constexpr double kMaxFps = 60;

int videoSide(const Json& params, const std::string& key, int fallback, int maximum) {
    const int side = optionalParam<int>(params, key).value_or(fallback);
    if (side < kMinVideoSide || side > maximum || side % 2 != 0) {
        throw AppError(std::format("give {} as an even number of pixels from {} to {}", key, kMinVideoSide, maximum));
    }
    return side;
}

Json renderVideo(Registry& registry, ProjectController& controller, Nav& nav, ViewEngine& views, const Json& params) {
    const std::filesystem::path file = registry.checkedPath(param<std::string>(params, "path"), PathAccess::Write);
    const std::filesystem::path ffmpeg = findFfmpeg();
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
    VideoRequest request{.file = file,
                         .width = videoSide(params, "width", VideoRequest{}.width, kMaxVideoWidth),
                         .height = videoSide(params, "height", VideoRequest{}.height, kMaxVideoHeight),
                         .fps = optionalParam<double>(params, "fps").value_or(VideoRequest{}.fps),
                         .from = optionalPosition(params, "from", tempo).value_or(0),
                         .to = std::min(optionalPosition(params, "to", tempo).value_or(end), end),
                         .currentView = view == "current"};
    if (request.fps < 1 || request.fps > kMaxFps) {
        throw AppError(std::format("give fps from 1 to {}", kMaxFps));
    }
    if (request.from < 0 || request.from >= request.to) {
        throw AppError(std::format("give from before to, both from {} to the project end {}",
                                   tempo.toBarBeatTick(0).format(), tempo.toBarBeatTick(end).format()));
    }
    const QTemporaryDir folder;
    if (!folder.isValid()) {
        throw AppError("cannot render: no temporary folder: " + folder.errorString().toStdString());
    }
    const std::filesystem::path mix = std::filesystem::path{folder.path().toStdString()} / "mix.wav";
    exportMix(session, registry, playbackContext(controller, document), mix, controller.sampleRate(),
              loudnessTarget(registry, params));
    const int frames = renderTimelineVideo(views, nav, controller, request, mix, ffmpeg);
    return {{"file", file.string()},
            {"frames", frames},
            {"seconds", rounded(tempo.ticksToSeconds(request.to) - tempo.ticksToSeconds(request.from), 0.001)},
            {"width", request.width},
            {"height", request.height},
            {"fps", request.fps},
            {"from", positionState(request.from, tempo)},
            {"to", positionState(request.to, tempo)}};
}

}  // namespace

void registerVideoActions(Registry& registry, ProjectController& controller, Nav& nav, ViewEngine& views) {
    registry.addAction(
        {.id = "video.render",
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
         .run = [&registry, &controller, &nav,
                 &views](const Json& params) { return renderVideo(registry, controller, nav, views, params); },
         .menus = {kFileMenu},
         .dialog = FileDialog{
             .param = "path", .mode = FileDialogMode::Save, .nameFilter = "Videos (*.mp4)", .suffix = "mp4"}});
}

}  // namespace emerick::app
