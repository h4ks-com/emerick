#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickItem>
#include <QString>
#include <vector>

#include "emerick/app/peak_cache.hpp"

namespace emerick::app {

// Draws an audio clip's peaks one column per pixel, only between visibleLeft and visibleRight, so a long clip costs the
// pixels on screen at any zoom. The item spans the whole clip; it holds no texture, so its width never matters.
class Waveform : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(emerick::app::PeakCache* peaks READ peaks WRITE setPeaks NOTIFY changed)
    Q_PROPERTY(QString file READ file WRITE setFile NOTIFY changed)
    Q_PROPERTY(qint64 offset READ offset WRITE setOffset NOTIFY changed)
    Q_PROPERTY(qint64 frames READ frames WRITE setFrames NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(double visibleLeft READ visibleLeft WRITE setVisibleLeft NOTIFY changed)
    Q_PROPERTY(double visibleRight READ visibleRight WRITE setVisibleRight NOTIFY changed)
    Q_PROPERTY(int drawnColumns READ drawnColumns NOTIFY changed)

public:
    explicit Waveform(QQuickItem* parent = nullptr);

    [[nodiscard]] PeakCache* peaks() const { return peaks_; }
    [[nodiscard]] QString file() const { return file_; }
    [[nodiscard]] qint64 offset() const { return offset_; }
    [[nodiscard]] qint64 frames() const { return frames_; }
    [[nodiscard]] QColor color() const { return color_; }
    [[nodiscard]] double visibleLeft() const { return visibleLeft_; }
    [[nodiscard]] double visibleRight() const { return visibleRight_; }
    // How many pixel columns the next frame draws.
    [[nodiscard]] int drawnColumns() const;

    void setPeaks(PeakCache* value);
    void setFile(const QString& value);
    void setOffset(qint64 value);
    void setFrames(qint64 value);
    void setColor(const QColor& value);
    void setVisibleLeft(double value);
    void setVisibleRight(double value);

signals:
    void changed();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& next, const QRectF& previous) override;

private:
    struct Column {
        double top;
        double bottom;
    };

    template <typename T>
    void assign(T& field, const T& value);
    [[nodiscard]] double firstColumn() const;
    [[nodiscard]] std::vector<Column> columns(const PeakLevels& levels) const;
    QSGNode* geometryNode(QSGNode* old, const std::vector<Column>& columns) const;
    QSGNode* imageNode(QSGNode* old, const std::vector<Column>& columns) const;

    QPointer<PeakCache> peaks_;
    QString file_;
    qint64 offset_ = 0;
    qint64 frames_ = 0;
    QColor color_;
    double visibleLeft_ = 0;
    double visibleRight_ = 0;
};

}  // namespace emerick::app
