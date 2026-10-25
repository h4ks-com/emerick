#include "block_strip.hpp"

#include <QColor>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace emerick::yamaha {

BlockStrip::BlockStrip(QQuickItem* parent) : QQuickPaintedItem(parent) {}

QRectF BlockStrip::blockRect(const QVariantMap& block) const {
    const double at = block.value("at").toDouble();
    const double end = block.value("end").toDouble();
    const double hairline = style_.value("hairline").toDouble();
    const double inset = style_.value("inset").toDouble();
    return {(at - from_) * pixelsPerTick_, inset, std::max(2 * hairline, (end - at) * pixelsPerTick_ - hairline),
            height() - 2 * inset};
}

std::pair<QFont, QString> BlockStrip::fitLabel(const QString& label, double width) const {
    QFont font;
    font.setWeight(QFont::Medium);
    const int smallest = style_.value("minimumPixelSize").toInt();
    for (int size = style_.value("pixelSize").toInt(); size > smallest; --size) {
        font.setPixelSize(size);
        if (QFontMetricsF{font}.horizontalAdvance(label) <= width) {
            return {font, label};
        }
    }
    font.setPixelSize(std::max(1, smallest));
    return {font, QFontMetricsF{font}.elidedText(label, Qt::ElideRight, width)};
}

QString BlockStrip::shownLabel(qint64 at) const {
    const double margins = 2 * style_.value("inset").toDouble() + style_.value("hairline").toDouble();
    for (const QVariant& entry : blocks_) {
        const QVariantMap block = entry.toMap();
        if (block.value("at").toLongLong() == at) {
            return fitLabel(block.value("label").toString(), blockRect(block).width() - margins).second;
        }
    }
    return {};
}

void BlockStrip::paint(QPainter* painter) {
    painter->setRenderHint(QPainter::Antialiasing);
    const QVariantList colors = style_.value("colors").toList();
    const double radius = style_.value("radius").toDouble();
    const double inset = style_.value("inset").toDouble();
    const double hairline = style_.value("hairline").toDouble();
    const double selection = style_.value("selectionWidth").toDouble();
    const auto ink = style_.value("ink").value<QColor>();
    const auto inkDim = style_.value("inkDim").value<QColor>();
    for (const QVariant& entry : blocks_) {
        const QVariantMap block = entry.toMap();
        const QRectF rect = blockRect(block);
        if (rect.right() < 0 || rect.left() > width()) {
            continue;
        }
        const bool gap = block.value("gap").toBool();
        const qint64 at = block.value("at").toLongLong();
        const bool picked =
            std::ranges::any_of(picked_, [at](const QVariant& tick) { return tick.toLongLong() == at; });
        if (!gap) {
            QPainterPath shape;
            shape.addRoundedRect(rect, radius, radius);
            painter->fillPath(shape, colors.value(block.value("colorIndex").toInt()).value<QColor>());
        }
        if (picked || gap) {
            const double pen = picked ? selection : hairline;
            painter->setPen(QPen{style_.value(picked ? "tape" : "rule").value<QColor>(), pen});
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(rect.adjusted(pen / 2, pen / 2, -pen / 2, -pen / 2), radius, radius);
        }
        const QRectF text = rect.adjusted(inset + hairline, 0, -inset, 0);
        if (text.width() <= 0) {
            continue;
        }
        const auto [font, shown] = fitLabel(block.value("label").toString(), text.width());
        painter->setFont(font);
        painter->setPen(gap ? inkDim : ink);
        painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, shown);
    }
}

}  // namespace emerick::yamaha
