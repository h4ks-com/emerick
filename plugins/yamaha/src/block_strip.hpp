#pragma once

#include <QtQmlIntegration/qqmlintegration.h>

#include <QFont>
#include <QQuickPaintedItem>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <utility>

namespace emerick::yamaha {

// Draws a lane's blocks in one item, from tick from on as wide as the item. Each block is a rounded rectangle in its
// color, or an outline for a gap, with its label shrunk to the small size or elided where it does not fit.
class BlockStrip : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList blocks READ blocks WRITE setBlocks NOTIFY changed)
    Q_PROPERTY(QVariantList picked READ picked WRITE setPicked NOTIFY changed)
    Q_PROPERTY(double from READ from WRITE setFrom NOTIFY changed)
    Q_PROPERTY(double pixelsPerTick READ pixelsPerTick WRITE setPixelsPerTick NOTIFY changed)
    // Theme values: colors, ink, inkDim, rule, tape, radius, inset, hairline, selectionWidth, pixelSize and
    // minimumPixelSize.
    Q_PROPERTY(QVariantMap style READ style WRITE setStyle NOTIFY changed)

public:
    explicit BlockStrip(QQuickItem* parent = nullptr);

    [[nodiscard]] QVariantList blocks() const { return blocks_; }
    [[nodiscard]] QVariantList picked() const { return picked_; }
    [[nodiscard]] double from() const { return from_; }
    [[nodiscard]] double pixelsPerTick() const { return pixelsPerTick_; }
    [[nodiscard]] QVariantMap style() const { return style_; }
    void setBlocks(const QVariantList& value) { assign(blocks_, value); }
    void setPicked(const QVariantList& value) { assign(picked_, value); }
    void setFrom(double value) { assign(from_, value); }
    void setPixelsPerTick(double value) { assign(pixelsPerTick_, value); }
    void setStyle(const QVariantMap& value) { assign(style_, value); }

    // We expose the label the block at tick at draws now, so a test can see whether it fits.
    Q_INVOKABLE [[nodiscard]] QString shownLabel(qint64 at) const;

    void paint(QPainter* painter) override;

signals:
    void changed();

private:
    template <class Value>
    void assign(Value& field, const Value& value) {
        if (field != value) {
            field = value;
            emit changed();
            update();
        }
    }
    [[nodiscard]] QRectF blockRect(const QVariantMap& block) const;
    [[nodiscard]] std::pair<QFont, QString> fitLabel(const QString& label, double width) const;

    QVariantList blocks_;
    QVariantList picked_;
    double from_ = 0;
    double pixelsPerTick_ = 0;
    QVariantMap style_;
};

}  // namespace emerick::yamaha
