#include "emerick/app/track_analysis.hpp"

#include <optional>
#include <ranges>

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
    pending_ = {track};
    analyzeNext();
}

void TrackAnalysis::analyzeAll() {
    if (!begin(kTracksAnalyzeAction, Json::object())) {
        return;
    }
    pending_ = analyzableTracks(registry(), controller_.project().document()) |
               std::views::transform([](const model::TrackId& id) { return qs(id.value); }) |
               std::ranges::to<QStringList>();
    analyzeNext();
}

void TrackAnalysis::analyzeNext() {
    if (pending_.isEmpty()) {
        return;
    }
    const QString track = pending_.takeFirst();
    render(
        track, kTrackAnalyzeAction,
        [this, &track] { return prepareTrackRender(controller_, registry(), {{"track", track.toStdString()}}); },
        [this, track](std::optional<RenderResult> result) {
            if (!result) {
                pending_.clear();
                return;
            }
            results_.insert(track, toVariant(result->json));
            emit resultsChanged();
            analyzeNext();
        });
}

}  // namespace emerick::app
