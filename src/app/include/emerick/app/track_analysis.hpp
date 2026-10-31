#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <thread>

#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::app {

// Adds track.analyze, which renders a track as its stem or the mix offline and measures its loudness and tone.
void registerTrackAnalysis(Registry& registry, ProjectController& controller);

// Runs track.analyze for the inspector on a worker thread, so the window keeps drawing while the track renders, and
// keeps each track's last result.
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
    void finish(const QString& track, const Json& result, const QString& error);

    const Registry& registry_;
    ProjectController& controller_;
    QString busyTrack_;
    QVariantMap results_;
    // Declared last, so it joins before the members its thread reads go away.
    std::jthread worker_;
};

}  // namespace emerick::app
