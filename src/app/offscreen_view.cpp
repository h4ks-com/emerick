#include "emerick/app/offscreen_view.hpp"

#include <rhi/qrhi.h>

#include <QGuiApplication>
#include <QQuickRenderTarget>
#include <QSizeF>
#include <cstring>
#include <format>

#include "emerick/app/registry.hpp"

namespace emerick::app {

struct OffscreenView::Target {
    QRhi* rhi = nullptr;
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiRenderBuffer> stencil;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::unique_ptr<QRhiRenderPassDescriptor> pass;
};

namespace {

// The offscreen platform has no RHI, so Qt Quick draws there with the software renderer, which grabs hidden windows.
bool drawsInSoftware() {
    return QGuiApplication::platformName() == QLatin1String("offscreen") ||
           QQuickWindow::sceneGraphBackend() == QLatin1String("software");
}

}  // namespace

OffscreenView::OffscreenView(QSize pixels) : pixels_(pixels) {
    if (drawsInSoftware()) {
        window_ = std::make_unique<QQuickWindow>();
    } else {
        control_ = std::make_unique<QQuickRenderControl>();
        window_ = std::make_unique<QQuickWindow>(control_.get());
    }
    window_->resize((QSizeF{pixels} / devicePixelRatio()).toSize());
    window_->contentItem()->setSize(window_->size());
}

OffscreenView::~OffscreenView() {
    target_.reset();
    window_.reset();
}

double OffscreenView::devicePixelRatio() const { return control_ ? 1.0 : window_->effectiveDevicePixelRatio(); }

void OffscreenView::startRhi() {
    if (!control_->initialize()) {
        throw AppError("cannot draw video frames: the graphics device did not start");
    }
    auto target = std::make_unique<Target>();
    target->rhi = control_->rhi();
    target->texture.reset(target->rhi->newTexture(QRhiTexture::RGBA8, pixels_, 1,
                                                  QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    target->stencil.reset(target->rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixels_, 1));
    const auto failed = [this] {
        return AppError(std::format("cannot draw video frames: no {}x{} texture", pixels_.width(), pixels_.height()));
    };
    if (!target->texture->create() || !target->stencil->create()) {
        throw failed();
    }
    QRhiTextureRenderTargetDescription description{QRhiColorAttachment{target->texture.get()}};
    description.setDepthStencilBuffer(target->stencil.get());
    target->target.reset(target->rhi->newTextureRenderTarget(description));
    target->pass.reset(target->target->newCompatibleRenderPassDescriptor());
    target->target->setRenderPassDescriptor(target->pass.get());
    if (!target->target->create()) {
        throw failed();
    }
    window_->setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(target->target.get()));
    target_ = std::move(target);
}

QImage OffscreenView::grabRhi() {
    if (!target_) {
        startRhi();
    }
    control_->polishItems();
    control_->beginFrame();
    control_->sync();
    control_->render();
    QRhiReadbackResult result;
    QRhiResourceUpdateBatch* readback = target_->rhi->nextResourceUpdateBatch();
    readback->readBackTexture(target_->texture.get(), &result);
    control_->commandBuffer()->resourceUpdate(readback);
    control_->endFrame();
    QImage image(pixels_, QImage::Format_RGBA8888);
    if (result.data.size() != image.sizeInBytes()) {
        throw AppError("cannot read back a video frame");
    }
    std::memcpy(image.bits(), result.data.constData(), static_cast<std::size_t>(result.data.size()));
    if (target_->rhi->isYUpInFramebuffer()) {
        image.flip(Qt::Vertical);
    }
    return image;
}

QImage OffscreenView::grab() {
    QImage image = control_ ? grabRhi() : window_->grabWindow();
    if (image.isNull()) {
        throw AppError("cannot draw a video frame");
    }
    if (image.size() != pixels_) {
        image = image.scaled(pixels_, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    image.convertTo(QImage::Format_RGB32);
    return image;
}

}  // namespace emerick::app
