#pragma once

#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <chrono>
#include <cstddef>
#include <vector>

#include "emerick/app/spectrogram.hpp"

namespace emerick::app {

// Draws a spectrogram's rows as a waterfall that scrolls down from the newest row at the top, with the newest
// spectrum and its decaying peaks in a band above it. The rows live in a ring image, and each frame uploads only the
// rows that arrived since the last one.
class SpectrogramView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(emerick::app::Spectrogram* monitor READ monitor WRITE setMonitor NOTIFY monitorChanged)
    Q_PROPERTY(double spectrumHeight READ spectrumHeight WRITE setSpectrumHeight NOTIFY layoutChanged)
    Q_PROPERTY(double waterfallTop READ waterfallTop NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList frequencyTicks READ frequencyTicks NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList timeTicks READ timeTicks NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList levelTicks READ levelTicks NOTIFY layoutChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor peakColor READ peakColor WRITE setPeakColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor bandColor READ bandColor WRITE setBandColor NOTIFY colorsChanged)
    Q_PROPERTY(int rowsDrawn READ rowsDrawn NOTIFY rowsDrawnChanged)

public:
    explicit SpectrogramView(QQuickItem* parent = nullptr);

    [[nodiscard]] Spectrogram* monitor() const { return monitor_; }
    void setMonitor(Spectrogram* value);
    [[nodiscard]] double spectrumHeight() const { return spectrumHeight_; }
    void setSpectrumHeight(double value);
    [[nodiscard]] double waterfallTop() const;
    [[nodiscard]] QVariantList frequencyTicks() const;
    [[nodiscard]] QVariantList timeTicks() const;
    [[nodiscard]] QVariantList levelTicks() const;
    [[nodiscard]] QColor lineColor() const { return lineColor_; }
    void setLineColor(const QColor& value);
    [[nodiscard]] QColor peakColor() const { return peakColor_; }
    void setPeakColor(const QColor& value);
    [[nodiscard]] QColor bandColor() const { return bandColor_; }
    void setBandColor(const QColor& value);
    // Every row the view drew since it last started over.
    [[nodiscard]] int rowsDrawn() const { return static_cast<int>(rowsDrawn_); }

    // The frequency, level, note and age under a point of the view, for the hover readout.
    Q_INVOKABLE QVariantMap readout(double x, double y) const;

signals:
    void monitorChanged();
    void layoutChanged();
    void colorsChanged();
    void rowsDrawnChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    struct ColumnBins {
        std::size_t first = 0;
        std::size_t last = 0;
        double fraction = 0.0;
    };

    void restart();
    void recolor();
    void pull();
    void addRow(const std::vector<float>& row);
    [[nodiscard]] double frequencyAt(double fraction) const;
    [[nodiscard]] double fractionOf(double hz) const;
    [[nodiscard]] double highestHz() const;
    [[nodiscard]] double rowHeight() const;
    [[nodiscard]] QRgb color(float db) const;
    [[nodiscard]] double spectrumY(float db) const;

    QPointer<Spectrogram> monitor_;
    SpectrogramSettings drawn_;
    double spectrumHeight_ = 0.0;
    QColor lineColor_;
    QColor peakColor_;
    QColor bandColor_;
    int columns_ = 0;
    int rows_ = 0;
    std::size_t head_ = 0;
    std::size_t filled_ = 0;
    std::size_t rowsDrawn_ = 0;
    std::vector<ColumnBins> columnBins_;
    std::vector<float> levels_;
    QImage image_;
    std::vector<float> spectrum_;
    std::vector<float> peakHold_;
    // The ring rows that changed since the scene graph last took them; a full upload replaces them all.
    std::vector<int> dirtyRows_;
    bool fullUpload_ = true;
    std::chrono::steady_clock::time_point lastPull_;
    QTimer timer_;
};

}  // namespace emerick::app
