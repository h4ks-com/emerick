#pragma once

#include <QObject>
#include <QTimer>
#include <memory>

#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/app/view_engine.hpp"

namespace emerick::app {

struct VideoJob;

// Runs video.render for the window one frame per event loop turn, so the window keeps drawing its progress.
class VideoRender : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)

public:
    VideoRender(Registry& registry, ProjectController& controller, Nav& nav, ViewEngine& views, QObject* parent);
    ~VideoRender() override;
    VideoRender(const VideoRender&) = delete;
    VideoRender& operator=(const VideoRender&) = delete;
    VideoRender(VideoRender&&) = delete;
    VideoRender& operator=(VideoRender&&) = delete;

    [[nodiscard]] bool running() const { return starting_ || job_ != nullptr; }
    // The share of the frames encoded, from 0 to 1.
    [[nodiscard]] double progress() const;
    // Takes video.render's params. Does nothing while a render runs; a failure raises a notice.
    void render(const Json& params);
    // Stops the render and deletes the unfinished file. False when no render runs.
    Q_INVOKABLE bool cancel();

signals:
    void runningChanged();
    void progressChanged();

private:
    void start();
    void step();
    void stop();
    void notifyFailure(const char* what) const;

    Registry& registry_;
    ProjectController& controller_;
    Nav& nav_;
    ViewEngine& views_;
    QTimer frames_;
    bool starting_ = false;
    Json params_;
    std::unique_ptr<VideoJob> job_;
};

}  // namespace emerick::app
