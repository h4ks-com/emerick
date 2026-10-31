#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/app/stem_export.hpp"

namespace emerick::app {

struct RenderResult {
    std::vector<std::vector<float>> channels;
    Json json;
};

// One offline render, which the UI thread sets up, and what to measure of its output on the thread that processes it.
struct RenderJob {
    std::unique_ptr<OfflineRender> render;
    engine::Frames frames = 0;
    std::function<RenderResult(std::vector<std::vector<float>>)> measure;
};

// UI thread: processes and measures the job.
RenderResult renderNow(RenderJob job);

// Runs one RenderJob at a time for the window. The UI thread sets up, idles and tears down the render, and a worker
// thread processes and measures it, so the window keeps drawing.
class BackgroundRender : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString busyTrack READ busyTrack NOTIFY busyTrackChanged)

public:
    [[nodiscard]] QString busyTrack() const { return busyTrack_; }

signals:
    void busyTrackChanged();

protected:
    BackgroundRender(const Registry& registry, QObject* parent);

    [[nodiscard]] const Registry& registry() const { return registry_; }
    // False while a render runs; otherwise records the action as run from the UI.
    bool begin(const char* actionId, const Json& params);
    // Calls done on the UI thread with the result, or with nothing once a failure raised a notice titled after the
    // action.
    void render(const QString& track, const char* actionId, const std::function<RenderJob()>& prepare,
                std::function<void(std::optional<RenderResult>)> done);
    void raiseNotice(const char* actionId, const QString& message) const;

private:
    // Worker thread: returns once the UI thread idled the render's instruments, or stop is requested.
    void idleOnUiThread(const std::stop_token& stop);
    void finish(RenderResult result, const QString& error);

    const Registry& registry_;
    QString busyTrack_;
    const char* actionId_ = nullptr;
    std::function<void(std::optional<RenderResult>)> done_;
    std::unique_ptr<OfflineRender> render_;
    // Declared last, so it joins before the members its thread reads go away.
    std::jthread worker_;
};

}  // namespace emerick::app
