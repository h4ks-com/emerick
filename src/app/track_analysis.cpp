#include "emerick/app/track_analysis.hpp"

#include <optional>
#include <ranges>

#include "emerick/app/analysis_render.hpp"
#include "emerick/app/project_state.hpp"

namespace emerick::app {

TrackAnalysis::TrackAnalysis(const Registry& registry, ProjectController& controller, QObject* parent)
    : BackgroundRender(registry, parent), controller_(controller) {}

std::vector<model::TrackId> analyzableTracks(const Registry& registry, const model::Document& document) {
    return document.tracks | std::views::filter([&](const model::Track& track) {
               const TrackType* type = registry.trackType(track.kind);
               return type != nullptr && playsAudio(*type, track);
           }) |
           std::views::transform(&model::Track::id) | std::ranges::to<std::vector>();
}

void TrackAnalysis::analyze(const QString& track) {
    if (!begin(kTrackAnalyzeAction, {{"track", track.toStdString()}})) {
        return;
    }
    run(track, kTrackAnalyzeAction,
        [this, &track] { return prepareTrackRender(controller_, registry(), {{"track", track.toStdString()}}); });
}

void TrackAnalysis::analyzeAll() {
    if (!begin(kTracksAnalyzeAction, Json::object())) {
        return;
    }
    run(qs(kTracksAnalyzeAction), kTracksAnalyzeAction,
        [this] { return prepareTracksRender(controller_, registry(), Json::object()); });
}

void TrackAnalysis::run(const QString& busy, const char* actionId, const std::function<RenderJob()>& prepare) {
    render(busy, actionId, prepare, [this](std::optional<RenderResult> result) {
        if (!result) {
            return;
        }
        for (const auto& [track, levels] : result->json.items()) {
            results_.insert(qs(track), toVariant(levels));
        }
        emit resultsChanged();
    });
}

}  // namespace emerick::app
