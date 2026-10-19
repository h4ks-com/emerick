#include "emerick/app/waveform.hpp"

#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGRendererInterface>
#include <algorithm>
#include <cmath>
#include <memory>
#include <span>

namespace emerick::app {

Waveform::Waveform(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents); }

template <typename T>
void Waveform::assign(T& field, const T& value) {
    if (field != value) {
        field = value;
        emit changed();
        update();
    }
}

void Waveform::setPeaks(PeakCache* value) {
    if (peaks_ == value) {
        return;
    }
    if (peaks_ != nullptr) {
        disconnect(peaks_, nullptr, this, nullptr);
    }
    peaks_ = value;
    if (peaks_ != nullptr) {
        connect(peaks_, &PeakCache::loaded, this, &QQuickItem::update);
    }
    emit changed();
    update();
}

void Waveform::setFile(const QString& value) { assign(file_, value); }
void Waveform::setOffset(qint64 value) { assign(offset_, value); }
void Waveform::setFrames(qint64 value) { assign(frames_, value); }
void Waveform::setColor(const QColor& value) { assign(color_, value); }
void Waveform::setVisibleLeft(double value) { assign(visibleLeft_, value); }
void Waveform::setVisibleRight(double value) { assign(visibleRight_, value); }

void Waveform::geometryChange(const QRectF& next, const QRectF& previous) {
    QQuickItem::geometryChange(next, previous);
    emit changed();
    update();
}

double Waveform::firstColumn() const { return std::floor(std::clamp(visibleLeft_, 0.0, width())); }

int Waveform::drawnColumns() const {
    const double last = std::ceil(std::clamp(visibleRight_, 0.0, width()));
    return static_cast<int>(std::max(0.0, last - firstColumn()));
}

std::vector<Waveform::Column> Waveform::columns(const PeakLevels& levels) const {
    const double framesPerPixel = static_cast<double>(frames_) / width();
    const double middle = height() / 2.0;
    const double first = firstColumn();
    std::vector<Column> result(static_cast<std::size_t>(drawnColumns()));
    for (std::size_t column = 0; column < result.size(); ++column) {
        const double x = first + static_cast<double>(column);
        const auto from = offset_ + std::llround(x * framesPerPixel);
        const auto to = std::max(from + 1, offset_ + std::llround((x + 1) * framesPerPixel));
        const auto range = peakRange(levels, from, to);
        if (!range) {
            result[column] = {middle, middle};
            continue;
        }
        const double top = middle - std::clamp(range->high, -1.0F, 1.0F) * middle;
        result[column] = {top, std::max(top + 1.0, middle - std::clamp(range->low, -1.0F, 1.0F) * middle)};
    }
    return result;
}

QSGNode* Waveform::geometryNode(QSGNode* old, const std::vector<Column>& columns) const {
    auto* node = dynamic_cast<QSGGeometryNode*>(old);
    if (node == nullptr) {
        node = std::make_unique<QSGGeometryNode>().release();
        auto geometry = std::make_unique<QSGGeometry>(QSGGeometry::defaultAttributes_Point2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(geometry.release());
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(std::make_unique<QSGFlatColorMaterial>().release());
        node->setFlag(QSGNode::OwnsMaterial);
    }
    auto* material = dynamic_cast<QSGFlatColorMaterial*>(node->material());
    if (material->color() != color_) {
        material->setColor(color_);
        node->markDirty(QSGNode::DirtyMaterial);
    }
    constexpr int kVerticesPerColumn = 6;
    QSGGeometry* geometry = node->geometry();
    geometry->allocate(kVerticesPerColumn * static_cast<int>(columns.size()));
    const std::span vertices{geometry->vertexDataAsPoint2D(), static_cast<std::size_t>(geometry->vertexCount())};
    const double first = firstColumn();
    for (std::size_t column = 0; column < columns.size(); ++column) {
        const auto left = static_cast<float>(first + static_cast<double>(column));
        const auto right = left + 1.0F;
        const auto top = static_cast<float>(columns[column].top);
        const auto bottom = static_cast<float>(columns[column].bottom);
        const auto quad = vertices.subspan(column * kVerticesPerColumn, kVerticesPerColumn);
        quad[0].set(left, top);
        quad[1].set(right, top);
        quad[2].set(left, bottom);
        quad[3].set(right, top);
        quad[4].set(right, bottom);
        quad[5].set(left, bottom);
    }
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}

// The software renderer draws no custom geometry, so there we paint the columns into an image of the visible part.
QSGNode* Waveform::imageNode(QSGNode* old, const std::vector<Column>& columns) const {
    QImage image{static_cast<int>(columns.size()), static_cast<int>(std::ceil(height())),
                 QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::transparent);
    {
        QPainter painter{&image};
        for (std::size_t column = 0; column < columns.size(); ++column) {
            painter.fillRect(QRectF{static_cast<double>(column), columns[column].top, 1.0,
                                    columns[column].bottom - columns[column].top},
                             color_);
        }
    }
    auto* node = dynamic_cast<QSGImageNode*>(old);
    if (node == nullptr) {
        node = window()->createImageNode();
        node->setOwnsTexture(true);
    }
    node->setTexture(window()->createTextureFromImage(image));
    node->setRect(QRectF{firstColumn(), 0, static_cast<double>(columns.size()), static_cast<double>(image.height())});
    return node;
}

QSGNode* Waveform::updatePaintNode(QSGNode* old, UpdatePaintNodeData* /*data*/) {
    const PeakLevels* levels = peaks_ != nullptr && !file_.isEmpty() ? peaks_->levels(file_.toStdString()) : nullptr;
    if (levels == nullptr || drawnColumns() <= 0 || frames_ <= 0 || height() < 1) {
        const std::unique_ptr<QSGNode> discarded{old};
        return nullptr;
    }
    const std::vector<Column> drawn = columns(*levels);
    const bool software = window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
    return software ? imageNode(old, drawn) : geometryNode(old, drawn);
}

}  // namespace emerick::app
