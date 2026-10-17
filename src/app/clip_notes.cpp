#include "emerick/app/clip_notes.hpp"

#include <QPainter>
#include <QVariantMap>
#include <algorithm>
#include <limits>

namespace emerick::app {

namespace {

// We spread the notes over at least an octave, so one held note draws as a bar and not as a block.
constexpr int kMinKeySpan = 12;
constexpr double kMinBarHeight = 1.5;

}  // namespace

ClipNotes::ClipNotes(QQuickItem* parent) : QQuickPaintedItem(parent) {}

void ClipNotes::setNotes(const QVariantList& value) {
    notes_ = value;
    emit notesChanged();
    update();
}

void ClipNotes::setLength(qint64 value) {
    if (length_ != value) {
        length_ = value;
        emit notesChanged();
        update();
    }
}

void ClipNotes::setColor(const QColor& value) {
    if (color_ != value) {
        color_ = value;
        emit notesChanged();
        update();
    }
}

void ClipNotes::paint(QPainter* painter) {
    if (notes_.isEmpty() || length_ <= 0) {
        return;
    }
    int low = std::numeric_limits<int>::max();
    int high = std::numeric_limits<int>::min();
    for (const QVariant& note : notes_) {
        const int key = note.toMap().value("key").toInt();
        low = std::min(low, key);
        high = std::max(high, key);
    }
    const int padding = std::max(0, kMinKeySpan - (high - low)) / 2;
    low -= padding;
    high += padding;
    const double rowHeight = height() / (high - low + 1);
    const double scale = width() / static_cast<double>(length_);
    for (const QVariant& note : notes_) {
        const QVariantMap fields = note.toMap();
        const double x = fields.value("at").toDouble() * scale;
        const double barWidth = std::max(1.0, fields.value("length").toDouble() * scale);
        const double y = (high - fields.value("key").toInt()) * rowHeight;
        painter->fillRect(QRectF{x, y, barWidth, std::max(kMinBarHeight, rowHeight * 0.8)}, color_);
    }
}

}  // namespace emerick::app
