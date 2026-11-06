#include "emerick/app/project_view.hpp"

#include <QPointF>
#include <algorithm>
#include <ranges>
#include <string>
#include <tuple>
#include <vector>

#include "emerick/core/fade.hpp"
#include "emerick/core/midi.hpp"
#include "emerick/core/plugin_format.hpp"
#include "emerick/engine/effects.hpp"
#include "emerick/model/controls.hpp"
#include "emerick/model/midi_notes.hpp"

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

Json insertState(const model::Insert& insert) {
    const engine::EffectSpec* spec = engine::findEffect(insert.type);
    Json state{{"id", insert.id.value},
               {"type", insert.type},
               {"title", spec != nullptr ? std::string{spec->title} : insert.type},
               {"bypass", insert.bypass},
               {"params", insert.params}};
    if (insert.plugin) {
        const std::string& vendor = insert.plugin->vendor;
        state["title"] = vendor.empty() || insert.plugin->name.starts_with(vendor) ? insert.plugin->name
                                                                                   : vendor + " " + insert.plugin->name;
        state["plugin"] = {{"id", insert.plugin->pluginId},
                           {"format", core::pluginFormatName(insert.plugin->format).id},
                           {"name", insert.plugin->name},
                           {"bundle", insert.plugin->bundle}};
    }
    return state;
}

namespace {

constexpr model::Control kSustain{.kind = model::ControlKind::controller, .number = core::midi::kSustainCc};

}  // namespace

Json laneChoices(const model::MidiClip& clip) {
    const std::vector<model::Control> present = model::controlsIn(clip);
    const auto choice = [&](std::string_view id, const std::string& title) {
        const auto control = model::parseControl(id);
        if (!control) {
            return Json{{"id", id},
                        {"title", title},
                        {"hasData", !clip.notes.empty()},
                        {"minimum", 1},
                        {"maximum", model::kMaxVelocity},
                        {"switched", false}};
        }
        return Json{{"id", id},
                    {"title", title},
                    {"hasData", std::ranges::contains(present, *control)},
                    {"minimum", model::controlMinimum(*control)},
                    {"maximum", model::controlMaximum(*control)},
                    {"switched", *control == kSustain}};
    };
    Json choices = Json::array();
    for (const LaneName& name : kStandardLanes) {
        choices.push_back(choice(name.id, std::string{name.title}));
    }
    for (const model::Control control : present) {
        const std::string id = model::controlId(control);
        if (!std::ranges::contains(kStandardLanes, std::string_view{id}, &LaneName::id)) {
            choices.push_back(choice(id, "CC " + std::to_string(control.number)));
        }
    }
    return choices;
}

}  // namespace emerick::app
