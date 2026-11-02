#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace emerick::viewer {

// A meter with no channels, which the level meter draws as empty room.
class SilentMeter : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<QObject*> channels READ channels CONSTANT)
    Q_PROPERTY(bool clipped READ clipped CONSTANT)
    Q_PROPERTY(double peakDb READ peakDb CONSTANT)

public:
    using QObject::QObject;
    [[nodiscard]] static QList<QObject*> channels() { return {}; }
    [[nodiscard]] static bool clipped() { return false; }
    [[nodiscard]] static double peakDb() { return kFloorDb; }

    static constexpr double kFloorDb = -60.0;
};

// The app's Meters singleton for a viewer that plays no tracks of its own.
class SilentMeters : public QObject {
    Q_OBJECT
    Q_PROPERTY(double floorDb READ floorDb CONSTANT)
    Q_PROPERTY(QVariantMap reductions READ reductions CONSTANT)

public:
    using QObject::QObject;
    [[nodiscard]] static double floorDb() { return SilentMeter::kFloorDb; }
    [[nodiscard]] static QVariantMap reductions() { return {}; }
    Q_INVOKABLE QObject* input(const QString& /*trackId*/) { return &silent_; }
    Q_INVOKABLE QObject* output(const QString& /*trackId*/) { return &silent_; }
    Q_INVOKABLE static double fraction(double db);

private:
    SilentMeter silent_;
};

}  // namespace emerick::viewer
