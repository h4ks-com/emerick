#include "emerick/app/track_analysis.hpp"

#include <condition_variable>
#include <mutex>
#include <ranges>
#include <stop_token>
#include <utility>
#include <vector>

#include "emerick/app/project_state.hpp"

namespace emerick::app {

TrackAnalysis::TrackAnalysis(const Registry& registry, ProjectController& controller, QObject* parent)
    : QObject(parent), registry_(registry), controller_(controller) {}

std::vector<model::TrackId> analyzableTracks(const Registry& registry, const model::Document& document) {
    return document.tracks | std::views::filter([&](const model::Track& track) {
               const TrackType* type = registry.trackType(track.kind);
               return type != nullptr && playsAudio(*type, track);
           }) |
           std::views::transform(&model::Track::id) | std::ranges::to<std::vector>();
}

void TrackAnalysis::analyze(const QString& track) {
    if (!busyTrack_.isEmpty()) {
        return;
    }
    emit registry_.actionRan(kTrackAnalyzeAction, {{"track", track.toStdString()}}, ActionSource::Ui);
    pending_ = {track};
    analyzeNext();
}

void TrackAnalysis::analyzeAll() {
    if (!busyTrack_.isEmpty()) {
        return;
    }
    emit registry_.actionRan(kTracksAnalyzeAction, Json::object(), ActionSource::Ui);
    pending_ = analyzableTracks(registry_, controller_.project().document()) |
               std::views::transform([](const model::TrackId& id) { return qs(id.value); }) |
               std::ranges::to<QStringList>();
    analyzeNext();
}

void TrackAnalysis::analyzeNext() {
    if (pending_.isEmpty()) {
        return;
    }
    const QString track = pending_.takeFirst();
    TrackRender job;
    try {
        job = prepareTrackRender(controller_, registry_, {{"track", track.toStdString()}});
    } catch (const AppError& error) {
        pending_.clear();
        emit registry_.noticeRaised(qs(registry_.action(kTrackAnalyzeAction)->title), qs(error.what()));
        return;
    }
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
    if (!error.isEmpty()) {
        pending_.clear();
        emit busyTrackChanged();
        emit registry_.noticeRaised(qs(registry_.action(kTrackAnalyzeAction)->title), error);
        return;
    }
    results_.insert(track, toVariant(result));
    emit resultsChanged();
    analyzeNext();
    if (busyTrack_.isEmpty()) {
        emit busyTrackChanged();
    }
}

}  // namespace emerick::app
