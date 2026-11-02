#pragma once

#include <QImage>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QSize>
#include <memory>

namespace emerick::app {

// Draws items into images of a fixed pixel size with no window on screen. On a desktop it renders through the RHI into
// a texture it reads back; on the offscreen platform the software renderer grabs a window that is never shown.
class OffscreenView {
public:
    explicit OffscreenView(QSize pixels);
    ~OffscreenView();
    OffscreenView(const OffscreenView&) = delete;
    OffscreenView& operator=(const OffscreenView&) = delete;
    OffscreenView(OffscreenView&&) = delete;
    OffscreenView& operator=(OffscreenView&&) = delete;

    // Items under it lay out in logical pixels, the image size over this ratio.
    [[nodiscard]] double devicePixelRatio() const;
    [[nodiscard]] QQuickItem* contentItem() const { return window_->contentItem(); }
    // The current frame in RGB32 at the pixel size.
    QImage grab();

private:
    struct Target;

    void startRhi();
    QImage grabRhi();

    QSize pixels_;
    std::unique_ptr<QQuickRenderControl> control_;
    std::unique_ptr<QQuickWindow> window_;
    std::unique_ptr<Target> target_;
};

}  // namespace emerick::app
