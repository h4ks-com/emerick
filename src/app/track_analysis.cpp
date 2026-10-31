#include "emerick/app/track_analysis.hpp"

#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <utility>
#include <vector>

#include "emerick/app/project_state.hpp"

namespace emerick::app {

TrackAnalysis::TrackAnalysis(const Registry& registry, ProjectController& controller, QObject* parent)
    : QObject(parent), registry_(registry), controller_(controller) {}

void TrackAnalysis::analyze(const QString& track) {
    if (!busyTrack_.isEmpty()) {
        return;
    }
    const Json params{{"track", track.toStdString()}};
    TrackRender job;
    try {
        job = prepareTrackRender(controller_, registry_, params);
    } catch (const AppError& error) {
        emit registry_.noticeRaised(qs(registry_.action(kTrackAnalyzeAction)->title), qs(error.what()));
        return;
    }
    emit registry_.actionRan(kTrackAnalyzeAction, params, ActionSource::Ui);
    busyTrack_ = track;
    emit busyTrackChanged();
    render_ = std::move(job.render);
    worker_ = std::jthread([this, track, range = std::move(job.range)](const std::stop_token& stop) {
        Json result;
        QString error;
        try {
            std::vector<std::vector<float>> channels =
                render_->process(range.endFrame(), [this, &stop] { idleOnUiThread(stop); }, stop);
            if (stop.stop_requested()) {
                return;
            }
            result = measureTrack(range, std::move(channels));
        } catch (const AppError& failure) {
            error = qs(failure.what());
        }
        QMetaObject::invokeMethod(
            this, [this, track, result = std::move(result), error] { finish(track, result, error); },
            Qt::QueuedConnection);
    });
}

void TrackAnalysis::idleOnUiThread(const std::stop_token& stop) {
    std::mutex mutex;
    std::condition_variable_any idled;
    bool done = false;
    QMetaObject::invokeMethod(
        this,
        [this, &mutex, &idled, &done] {
            render_->idleInstruments();
            const std::scoped_lock lock(mutex);
            done = true;
            idled.notify_one();
        },
        Qt::QueuedConnection);
    std::unique_lock lock(mutex);
    idled.wait(lock, stop, [&done] { return done; });
}

void TrackAnalysis::finish(const QString& track, const Json& result, const QString& error) {
    worker_.join();
    render_.reset();
    busyTrack_.clear();
    emit busyTrackChanged();
    if (!error.isEmpty()) {
        emit registry_.noticeRaised(qs(registry_.action(kTrackAnalyzeAction)->title), error);
        return;
    }
    results_.insert(track, toVariant(result));
    emit resultsChanged();
}

}  // namespace emerick::app
