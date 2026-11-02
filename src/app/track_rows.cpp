#include "emerick/app/track_rows.hpp"

#include <array>
#include <ranges>
#include <string>
#include <utility>
#include <variant>

#include "emerick/app/variant.hpp"
#include "emerick/core/automation.hpp"
#include "emerick/core/fade.hpp"
#include "emerick/core/overloaded.hpp"
#include "emerick/model/clip_edit.hpp"
#include "emerick/model/midi_notes.hpp"

namespace emerick::app {

namespace {

constexpr std::array kLaneScaleNames{"linear", "logarithmic", "fader"};

QVariant pointEntry(const model::AutomationPoint& point) {
    return QVariantMap{{"id", qs(point.id.value)},
                       {"tick", QVariant::fromValue(point.tick)},
                       {"value", point.value},
                       {"curve", qs(std::string{core::automationCurveId(point.curve)})}};
}

}  // namespace

QVariantList noteList(const model::MidiClip& clip, core::Ticks from) {
    return clip.notes | std::views::transform([from](const model::Note& note) {
               return QVariant{QVariantMap{{"id", qs(note.id.value)},
                                           {"pitch", note.pitch},
                                           {"velocity", note.velocity},
                                           {"start", QVariant::fromValue(from + note.start)},
                                           {"length", QVariant::fromValue(note.length)},
                                           {"channel", note.channel}}};
           }) |
           std::ranges::to<QVariantList>();
}

QVariantMap clipEntry(const model::Clip& clip, int take, const RowContext& context) {
    const core::TempoMap& tempo = context.tempo;
    const core::Ticks start = model::clipStart(clip);
    const core::Ticks end = model::clipEnd(clip);
    QVariantMap entry{{"id", qs(model::clipId(clip).value)},
                      {"take", take},
                      {"muted", model::clipMuted(clip)},
                      {"name", qs(model::clipName(clip))},
                      {"start", QVariant::fromValue(start)},
                      {"length", QVariant::fromValue(model::clipLength(clip))},
                      {"end", QVariant::fromValue(end)},
                      {"startText", qs(tempo.toBarBeatTick(start).format())},
                      {"endText", qs(tempo.toBarBeatTick(end).format())},
                      {"lengthText", qs(tempo.durationText(end - start))}};
    std::visit(
        core::Overloaded{
            [&](const model::AudioClip& audio) {
                const std::filesystem::path file{audio.file};
                const core::Frames frames =
                    tempo.ticksToFrames(end, context.sampleRate) - tempo.ticksToFrames(start, context.sampleRate);
                entry.insert(QVariantMap{{"type", "audio"},
                                         {"fadeIn", QVariant::fromValue(audio.fadeIn)},
                                         {"fadeOut", QVariant::fromValue(audio.fadeOut)},
                                         {"fadeInShape", qs(std::string{core::fadeShapeId(audio.fadeInShape)})},
                                         {"fadeOutShape", qs(std::string{core::fadeShapeId(audio.fadeOutShape)})},
                                         {"fadeInCurve", audio.fadeInCurve},
                                         {"fadeOutCurve", audio.fadeOutCurve},
                                         {"gainDb", audio.gainDb},
                                         {"file", qs((file.is_absolute() ? file : context.folder / file).string())},
                                         {"sourceOffset", QVariant::fromValue(audio.sourceOffset)},
                                         {"frames", QVariant::fromValue(frames)}});
            },
            [&](const model::MidiClip& midi) {
                entry.insert(QVariantMap{{"type", "midi"}, {"notes", noteList(midi)}, {"looped", model::looped(midi)}});
            }},
        clip);
    return entry;
}

QVariantList clipList(const model::Track& track, const RowContext& context) {
    QVariantList clips;
    int take = 0;
    for (const model::Clip& clip : track.clips) {
        clips.append(clipEntry(clip, ++take, context));
    }
    return clips;
}

QVariantList laneList(const model::Track& track) {
    return track.automation | std::views::transform([](const model::AutomationLane& lane) {
               return QVariant{QVariantMap{
                   {"param", qs(lane.param)},
                   {"points", lane.points | std::views::transform(pointEntry) | std::ranges::to<QVariantList>()}}};
           }) |
           std::ranges::to<QVariantList>();
}

QVariantList paramList(std::span<const AutomationParam> params) {
    return params | std::views::transform([](const AutomationParam& param) {
               return QVariant{QVariantMap{{"id", qs(param.id)},
                                           {"title", qs(param.title)},
                                           {"minimum", param.minimum},
                                           {"maximum", param.maximum},
                                           {"value", param.value},
                                           {"toggle", param.toggle},
                                           {"unit", qs(param.unit)},
                                           {"scale", kLaneScaleNames.at(std::to_underlying(param.scale))}}};
           }) |
           std::ranges::to<QVariantList>();
}

}  // namespace emerick::app
