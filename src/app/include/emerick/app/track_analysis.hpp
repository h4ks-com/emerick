#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>
#include <stop_token>
#include <thread>

#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/app/stem_export.hpp"

namespace emerick::app {

// Adds track.analyze, which renders a track as its stem or the mix offline and measures its loudness and tone.
void registerTrackAnalysis(Registry& registry, ProjectController& controller);

// Runs track.analyze for the inspector and keeps each track's last result. The UI thread sets up and tears down the
// render and idles its instruments, and a worker thread processes it and measures, so the window keeps drawing.
class TrackAnalysis : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString busyTrack READ busyTrack NOTIFY busyTrackChanged)
    Q_PROPERTY(QVariantMap results READ results NOTIFY resultsChanged)

public:
    TrackAnalysis(const Registry& registry, ProjectController& controller, QObject* parent = nullptr);

    [[nodiscard]] QString busyTrack() const { return busyTrack_; }
    [[nodiscard]] QVariantMap results() const { return results_; }
    // Does nothing while another analysis runs; a failure raises a notice.
    Q_INVOKABLE void analyze(const QString& track);

signals:
    void busyTrackChanged();
    void resultsChanged();

private:
    // Worker thread: returns once the UI thread idled the render's instruments, or stop is requested.
    void idleOnUiThread(const std::stop_token& stop);
    void finish(const QString& track, const Json& result, const QString& error);

    const Registry& registry_;
    ProjectController& controller_;
    QString busyTrack_;
    QVariantMap results_;
    std::unique_ptr<OfflineRender> render_;
    // Declared last, so it joins before the members its thread reads go away.
    std::jthread worker_;
};

}  // namespace emerick::app
