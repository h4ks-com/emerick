#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace emerick::app {

// The built-in effects' params for the effect editors, and the Channel EQ's response curve from the engine's own
// filters.
class EffectCatalog : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Each param as {id, title, unit, minimum, maximum, defaultValue, kind, logarithmic, choices}.
    Q_INVOKABLE static QVariantList params(const QString& type);
    // The response in dB at points frequencies spaced evenly on a log scale from 20 Hz to 20 kHz, as {x, db} with x
    // from 0 to 1. Missing params take their defaults.
    Q_INVOKABLE static QVariantList eqCurve(const QVariantMap& params, int points);
};

}  // namespace emerick::app
