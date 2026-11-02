#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "emerick/app/variant.hpp"
#include "emerick/core/time.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// What the views read from a project beside its track rows, for the app's Project singleton and the viewer's.
QString positionText(const core::TempoMap& tempo, core::Ticks ticks);
QString tempoText(const core::TempoMap& tempo);
QString ticksText(const core::TempoMap& tempo, core::Ticks ticks, bool duration);
QVariantMap cycleEntry(const model::Cycle& cycle);
// Points along a fade from 0 to 1, which the clips draw.
QVariantList fadeCurvePoints(const QString& shape, double curve);
double fadeCurveThroughMidpoint(const QString& shape, double gain);

// Number counts the markers from 1 in position order, as the go to marker keys do.
Json markerState(const model::Marker& marker, int number, const core::TempoMap& tempo);
Json markersState(const model::Document& document);

}  // namespace emerick::app
