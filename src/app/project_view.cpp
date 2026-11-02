#include "emerick/app/project_view.hpp"

#include <QPointF>
#include <ranges>
#include <tuple>

#include "emerick/core/fade.hpp"

namespace emerick::app {

namespace {

core::FadeShape shapeOf(const QString& shape) {
    return core::fadeShapeFromId(shape.toStdString()).value_or(core::FadeShape::linear);
}

}  // namespace

QString positionText(const core::TempoMap& tempo, core::Ticks ticks) {
    const core::BarBeatTick position = tempo.toBarBeatTick(ticks);
    return QStringLiteral("%1 . %2 . %3").arg(position.bar).arg(position.beat).arg(position.tick, 3, 10, QChar('0'));
}

QString tempoText(const core::TempoMap& tempo) {
    return QStringLiteral("%1   %2/%3").arg(tempo.bpm).arg(tempo.meter.numerator).arg(tempo.meter.denominator);
}

QString ticksText(const core::TempoMap& tempo, core::Ticks ticks, bool duration) {
    return qs(duration ? tempo.durationText(ticks) : tempo.toBarBeatTick(ticks).format());
}

QVariantMap cycleEntry(const model::Cycle& cycle) {
    return {{"enabled", cycle.enabled},
            {"start", QVariant::fromValue<qint64>(cycle.start)},
            {"end", QVariant::fromValue<qint64>(cycle.end)}};
}

QVariantList fadeCurvePoints(const QString& shape, double curve) {
    constexpr int kSteps = 32;
    const core::FadeCurve fade = core::fadeCurve(shapeOf(shape), curve);
    return std::views::iota(0, kSteps + 1) | std::views::transform([&](int step) {
               const float t = static_cast<float>(step) / kSteps;
               return QVariant{QPointF{t, core::fadeCurveGain(fade, t)}};
           }) |
           std::ranges::to<QVariantList>();
}

double fadeCurveThroughMidpoint(const QString& shape, double gain) {
    return core::curveThroughMidpoint(shapeOf(shape), gain);
}

Json markerState(const model::Marker& marker, int number, const core::TempoMap& tempo) {
    return {{"id", marker.id.value},
            {"name", marker.name},
            {"number", number},
            {"at", marker.at},
            {"position", tempo.toBarBeatTick(marker.at).format()},
            {"color", marker.color ? Json(*marker.color) : Json(nullptr)}};
}

Json markersState(const model::Document& document) {
    return std::views::zip(std::views::iota(1), document.markers) | std::views::transform([&](const auto& entry) {
               return markerState(std::get<1>(entry), std::get<0>(entry), document.tempo);
           }) |
           std::ranges::to<Json::array_t>();
}

}  // namespace emerick::app
