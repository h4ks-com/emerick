#include "emerick/app/transport_clock.hpp"

namespace emerick::app {

TransportClock::TransportClock(const ProjectController& controller, QObject* parent)
    : QObject(parent), controller_(controller) {
    connect(&controller, &ProjectController::transportChanged, this, &TransportClock::changed);
    connect(&controller, &ProjectController::projectChanged, this, &TransportClock::changed);
}

QString TransportClock::meter() const {
    const core::Meter& meter = controller_.project().document().tempo.meter;
    return QStringLiteral("%1/%2").arg(meter.numerator).arg(meter.denominator);
}

core::BarBeatTick TransportClock::position() const {
    return controller_.project().document().tempo.toBarBeatTick(controller_.positionTicks());
}

}  // namespace emerick::app
