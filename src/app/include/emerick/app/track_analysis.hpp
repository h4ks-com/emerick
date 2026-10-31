#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <vector>

#include "emerick/app/background_render.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::app {

inline constexpr const char* kTrackAnalyzeAction = "track.analyze";
inline constexpr const char* kTracksAnalyzeAction = "tracks.analyze";

// UI thread: checks track.analyze's params and sets up the render of its track and its measurement.
RenderJob prepareTrackRender(ProjectController& controller, const Registry& registry, const Json& params);
// The audio tracks and buses in track order.
std::vector<model::TrackId> analyzableTracks(const Registry& registry, const model::Document& document);

// Runs track.analyze for the inspector in the background and keeps each track's last result.
class TrackAnalysis : public BackgroundRender {
    Q_OBJECT
    Q_PROPERTY(QVariantMap results READ results NOTIFY resultsChanged)

public:
    TrackAnalysis(const Registry& registry, ProjectController& controller, QObject* parent = nullptr);

    [[nodiscard]] QVariantMap results() const { return results_; }
    // Does nothing while another analysis runs; a failure raises a notice.
    Q_INVOKABLE void analyze(const QString& track);
    // Analyzes each of analyzableTracks in turn, as tracks.analyze does, and stops at the first failure.
    Q_INVOKABLE void analyzeAll();

signals:
    void resultsChanged();

private:
    void analyzeNext();

    ProjectController& controller_;
    QStringList pending_;
    QVariantMap results_;
};

}  // namespace emerick::app
