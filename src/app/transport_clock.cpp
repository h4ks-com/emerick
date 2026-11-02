#include "emerick/app/transport_clock.hpp"

namespace emerick::app {

void TransportClock::show(const core::TempoMap& tempo, core::Ticks position) {
    if (tempo != tempo_ || position != position_) {
        tempo_ = tempo;
        position_ = position;
        emit changed();
    }
}

QString TransportClock::meter() const {
    return QStringLiteral("%1/%2").arg(tempo_.meter.numerator).arg(tempo_.meter.denominator);
}

}  // namespace emerick::app
