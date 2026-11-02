#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <array>
#include <string_view>

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
// The insert with its effect's title and every param by id; automation names a param as "<insert id>.<param id>".
Json insertState(const model::Insert& insert);

inline constexpr std::string_view kVelocityLane = "velocity";

struct LaneName {
    std::string_view id;
    std::string_view title;
};

// The piano roll's controller lanes in the order its lane picker lists them.
inline constexpr std::array kStandardLanes{
    LaneName{kVelocityLane, "Velocity"}, LaneName{"cc1", "Modulation"},  LaneName{"pitchBend", "Pitch bend"},
    LaneName{"cc64", "Sustain"},         LaneName{"cc11", "Expression"}, LaneName{"pressure", "Aftertouch"},
};

// The lanes the piano roll offers for the clip as {id, title, hasData}: the standard lanes, then any other controller
// the clip holds.
Json laneChoices(const model::MidiClip& clip);

}  // namespace emerick::app
