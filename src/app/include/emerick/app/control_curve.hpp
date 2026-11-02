#pragma once

#include <QColor>
#include <QList>
#include <QPointF>
#include <QQuickPaintedItem>
#include <span>
#include <vector>

namespace emerick::app {

// Dense data reads as a line, so a point gets a dot only where both neighbours stand this many pixels away.
inline constexpr double kDotSpacing = 16.0;

// The points, in x order, that stand at least kDotSpacing from both neighbours.
std::vector<QPointF> spacedDots(std::span<const QPointF> points);

// Draws a controller lane's values as steps that hold until the next point, filled from the bottom or, when the
// minimum is below zero, from the center. A switched lane, such as sustain, draws each on stretch as a block. Points
// arrive flat as at, value, at, value in time order, with at in ticks from the clip start. A point gets a dot where it
// stands apart from its neighbours and while it is the hovered point.
class ControlCurve : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QList<double> points READ points WRITE setPoints NOTIFY curveChanged)
    Q_PROPERTY(int minimum MEMBER minimum_ WRITE setMinimum NOTIFY curveChanged)
    Q_PROPERTY(int maximum MEMBER maximum_ WRITE setMaximum NOTIFY curveChanged)
    Q_PROPERTY(bool switched MEMBER switched_ WRITE setSwitched NOTIFY curveChanged)
    Q_PROPERTY(double originTicks MEMBER originTicks_ WRITE setOriginTicks NOTIFY curveChanged)
    Q_PROPERTY(double pixelsPerTick MEMBER pixelsPerTick_ WRITE setPixelsPerTick NOTIFY curveChanged)
    Q_PROPERTY(double length MEMBER length_ WRITE setLength NOTIFY curveChanged)
    Q_PROPERTY(QColor color MEMBER color_ WRITE setColor NOTIFY curveChanged)
    Q_PROPERTY(int hovered MEMBER hovered_ WRITE setHovered NOTIFY curveChanged)
    Q_PROPERTY(int count READ count NOTIFY curveChanged)

public:
    explicit ControlCurve(QQuickItem* parent = nullptr);

    [[nodiscard]] QList<double> points() const { return points_; }
    [[nodiscard]] int count() const { return static_cast<int>(steps_.size()); }
    void setPoints(const QList<double>& value);
    void setMinimum(int value) { assign(minimum_, value); }
    void setMaximum(int value) { assign(maximum_, value); }
    void setSwitched(bool value) { assign(switched_, value); }
    void setOriginTicks(double value) { assign(originTicks_, value); }
    void setPixelsPerTick(double value) { assign(pixelsPerTick_, value); }
    void setLength(double value) { assign(length_, value); }
    void setColor(const QColor& value) { assign(color_, value); }
    void setHovered(int value) { assign(hovered_, value); }

    // The index of the point nearest x within slack pixels, or -1.
    Q_INVOKABLE int indexNear(double x, double slack) const;
    Q_INVOKABLE double atOf(int index) const;
    Q_INVOKABLE int valueOf(int index) const;
    Q_INVOKABLE double xOf(double ticks) const;
    Q_INVOKABLE double yOf(double value) const;
    // The value at height y, clamped to the lane's range.
    Q_INVOKABLE int valueAt(double y) const;

    void paint(QPainter* painter) override;

signals:
    void curveChanged();

private:
    struct Step {
        double at = 0;
        int value = 0;
    };

    template <class T>
    void assign(T& field, const T& value) {
        if (field != value) {
            field = value;
            emit curveChanged();
            update();
        }
    }
    void paintBlocks(QPainter* painter) const;
    void paintSteps(QPainter* painter) const;

    QList<double> points_;
    std::vector<Step> steps_;
    int minimum_ = 0;
    int maximum_ = 127;
    bool switched_ = false;
    double originTicks_ = 0;
    double pixelsPerTick_ = 1;
    double length_ = 0;
    QColor color_;
    int hovered_ = -1;
};

}  // namespace emerick::app
