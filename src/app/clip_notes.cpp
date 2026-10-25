#include "emerick/app/clip_notes.hpp"

#include <QPainter>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace emerick::app {

namespace {

// We spread the notes over at least an octave, so one held note draws as a bar and not as a block.
constexpr int kMinKeySpan = 12;
constexpr double kMinBarHeight = 1.5;
constexpr int kKeys = 128;

}  // namespace

ClipNotes::ClipNotes(QQuickItem* parent) : QQuickPaintedItem(parent) {}

void ClipNotes::setNotes(const QVariantList& value) {
    if (notes_ == value) {
        return;
    }
    notes_ = value;
    bars_.clear();
    bars_.reserve(static_cast<std::size_t>(notes_.size()));
    for (const QVariant& note : notes_) {
        const QVariantMap fields = note.toMap();
        bars_.push_back({.start = fields.value("start").toDouble(),
                         .length = fields.value("length").toDouble(),
                         .pitch = std::clamp(fields.value("pitch").toInt(), 0, kKeys - 1)});
    }
    std::ranges::stable_sort(bars_, {}, &Bar::start);
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

void ClipNotes::setKey(int& key, int value) {
    if (key != value) {
        key = value;
        emit notesChanged();
        update();
    }
}

void ClipNotes::setLowKey(int value) { setKey(lowKey_, value); }

void ClipNotes::setHighKey(int value) { setKey(highKey_, value); }

void ClipNotes::paint(QPainter* painter) {
    if (bars_.empty() || length_ <= 0) {
        return;
    }
    int low = lowKey_;
    int high = highKey_;
    if (low < 0 || high < low) {
        const auto [lowest, highest] = std::ranges::minmax(bars_, {}, &Bar::pitch);
        low = lowest.pitch;
        high = highest.pitch;
    }
    const int padding = std::max(0, kMinKeySpan - (high - low)) / 2;
    low -= padding;
    high += padding;
    const double rowHeight = height() / (high - low + 1);
    const double barHeight = std::max(kMinBarHeight, rowHeight * 0.8);
    const double scale = width() / static_cast<double>(length_);
    // Zoomed out, many notes share a pixel, so we draw each key's pixels once.
    std::array<double, kKeys> drawnTo{};
    drawnTo.fill(std::numeric_limits<double>::lowest());
    for (const Bar& bar : bars_) {
        const double left = std::floor(bar.start * scale);
        const double right = std::max(left + 1.0, std::ceil((bar.start + bar.length) * scale));
        double& drawn = drawnTo.at(static_cast<std::size_t>(bar.pitch));
        if (right <= drawn) {
            continue;
        }
        drawn = right;
        painter->fillRect(QRectF{left, (high - bar.pitch) * rowHeight, right - left, barHeight}, color_);
    }
}

}  // namespace emerick::app
