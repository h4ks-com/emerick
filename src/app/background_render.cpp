#include "emerick/app/background_render.hpp"

#include <condition_variable>
#include <mutex>
#include <utility>

namespace emerick::app {

RenderResult renderNow(RenderJob job) {
    if (job.play) {
        job.play(job.sink);
    } else {
        OfflineRender& render = *job.render;
        render.process(job.frames, job.sink, [&render] { render.idleInstruments(); });
    }
    return job.finish();
}

BackgroundRender::BackgroundRender(const Registry& registry, QObject* parent) : QObject(parent), registry_(registry) {}

bool BackgroundRender::begin(const char* actionId, const Json& params) {
    if (!busyTrack_.isEmpty()) {
        return false;
    }
    emit registry_.actionRan(actionId, params, ActionSource::Ui);
    return true;
}

void BackgroundRender::raiseNotice(const char* actionId, const QString& message) const {
    emit registry_.noticeRaised(qs(registry_.action(actionId)->title), message, {});
}

void BackgroundRender::render(const QString& track, const char* actionId, const std::function<RenderJob()>& prepare,
                              std::function<void(std::optional<RenderResult>)> done) {
    RenderJob job;
    try {
        job = prepare();
    } catch (const AppError& error) {
        raiseNotice(actionId, qs(error.what()));
        done(std::nullopt);
        return;
    }
    busyTrack_ = track;
    actionId_ = actionId;
    done_ = std::move(done);
    render_ = std::move(job.render);
    emit busyTrackChanged();
    worker_ = std::jthread([this, frames = job.frames, sink = std::move(job.sink),
                            measure = std::move(job.finish)](const std::stop_token& stop) {
        RenderResult result;
        QString error;
        try {
            render_->process(frames, sink, [this, &stop] { idleOnUiThread(stop); }, stop);
            if (stop.stop_requested()) {
                return;
            }
            result = measure();
        } catch (const AppError& failure) {
            error = qs(failure.what());
        }
        QMetaObject::invokeMethod(
            this, [this, result = std::move(result), error] { finish(result, error); }, Qt::QueuedConnection);
    });
}

void BackgroundRender::idleOnUiThread(const std::stop_token& stop) {
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

void BackgroundRender::finish(RenderResult result, const QString& error) {
    worker_.join();
    render_.reset();
    busyTrack_.clear();
    const std::function<void(std::optional<RenderResult>)> done = std::exchange(done_, {});
    if (error.isEmpty()) {
        done(std::move(result));
    } else {
        raiseNotice(actionId_, error);
        done(std::nullopt);
    }
    if (busyTrack_.isEmpty()) {
        emit busyTrackChanged();
    }
}

}  // namespace emerick::app
