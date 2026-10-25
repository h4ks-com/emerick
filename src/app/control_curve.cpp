#include "emerick/app/control_curve.hpp"

#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace emerick::app {

namespace {

constexpr double kFillAlpha = 0.3;
constexpr double kLineWidth = 1.5;
constexpr double kDotRadius = 2.5;
// We draw a point's dot only where it stands this far from the last dot, so dense recordings read as a line.
constexpr double kDotSpacing = 8.0;
constexpr int kSwitchedOn = 64;

}  // namespace

ControlCurve::ControlCurve(QQuickItem* parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }

void ControlCurve::setPoints(const QList<double>& value) {
    if (points_ == value) {
        return;
    }
    points_ = value;
    steps_.clear();
    steps_.reserve(static_cast<std::size_t>(points_.size() / 2));
    for (qsizetype index = 0; index + 1 < points_.size(); index += 2) {
        steps_.push_back({.at = points_[index], .value = static_cast<int>(points_[index + 1])});
    }
    emit curveChanged();
    update();
}

double ControlCurve::xOf(double ticks) const { return (ticks - originTicks_) * pixelsPerTick_; }

double ControlCurve::yOf(double value) const {
    const double span = std::max(1, maximum_ - minimum_);
    return (1.0 - (value - minimum_) / span) * height();
}

int ControlCurve::valueAt(double y) const {
    const double share = height() > 0 ? 1.0 - y / height() : 0.0;
    return std::clamp(static_cast<int>(std::lround(minimum_ + share * (maximum_ - minimum_))), minimum_, maximum_);
}

double ControlCurve::atOf(int index) const {
    return index >= 0 && index < count() ? steps_[static_cast<std::size_t>(index)].at : 0;
}

int ControlCurve::valueOf(int index) const {
    return index >= 0 && index < count() ? steps_[static_cast<std::size_t>(index)].value : 0;
}

int ControlCurve::indexNear(double x, double slack) const {
    const double ticks = x / pixelsPerTick_ + originTicks_;
    const auto after = std::ranges::lower_bound(steps_, ticks, {}, &Step::at);
    int best = -1;
    double bestDistance = slack;
    for (auto candidate = after == steps_.begin() ? after : after - 1; candidate != steps_.end() && candidate <= after;
         ++candidate) {
        const double distance = std::abs(xOf(candidate->at) - x);
        if (distance <= bestDistance) {
            best = static_cast<int>(candidate - steps_.begin());
            bestDistance = distance;
        }
    }
    return best;
}

void ControlCurve::paint(QPainter* painter) {
    if (steps_.empty() || pixelsPerTick_ <= 0) {
        return;
    }
    if (switched_) {
        paintBlocks(painter);
    } else {
        paintSteps(painter);
    }
}

void ControlCurve::paintBlocks(QPainter* painter) const {
    const double end = xOf(length_);
    for (auto step = steps_.begin(); step != steps_.end(); ++step) {
        if (step->value < kSwitchedOn) {
            continue;
        }
        const auto next =
            std::ranges::find_if(step + 1, steps_.end(), [](const Step& later) { return later.value < kSwitchedOn; });
        const double left = xOf(step->at);
        const double right = next == steps_.end() ? end : xOf(next->at);
        if (right >= 0 && left <= width()) {
            painter->fillRect(QRectF{left, 0, std::max(1.0, right - left), height()}, color_);
        }
        step = next == steps_.end() ? std::prev(steps_.end()) : next;
    }
}

// Zoomed out, many points share a pixel column, so each column keeps only its highest and lowest value and the value
// it ends on, and the points left of the view fold into one column.
void ControlCurve::paintSteps(QPainter* painter) const {
    const double right = width() + 1;
    QPolygonF line;
    QList<QPointF> dots;
    double column = 0;
    double top = 0;
    double bottom = 0;
    double last = 0;
    for (const Step& step : steps_) {
        const double x = std::clamp(xOf(step.at), -1.0, right);
        const double y = yOf(step.value);
        if (!line.isEmpty() && x - column < 1.0) {
            if (y < top || y > bottom) {
                line << QPointF{column, y};
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
            last = y;
            continue;
        }
        if (!line.isEmpty()) {
            line << QPointF{column, last} << QPointF{x, last};
        }
        line << QPointF{x, y};
        if (dots.isEmpty() || x - dots.back().x() >= kDotSpacing) {
            dots << QPointF{x, y};
        }
        column = x;
        top = y;
        bottom = y;
        last = y;
        if (x >= right) {
            break;
        }
    }
    line << QPointF{column, last} << QPointF{std::clamp(xOf(length_), column, right), last};

    const double baseline = minimum_ < 0 ? yOf(0) : height();
    QPolygonF area = line;
    area << QPointF{line.back().x(), baseline} << QPointF{line.front().x(), baseline};
    QColor fill = color_;
    fill.setAlphaF(static_cast<float>(kFillAlpha));
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawPolygon(area);
    painter->setPen(QPen{color_, kLineWidth});
    painter->setBrush(Qt::NoBrush);
    painter->drawPolyline(line);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color_);
    for (const QPointF& dot : dots) {
        painter->drawEllipse(dot, kDotRadius, kDotRadius);
    }
}

}  // namespace emerick::app
