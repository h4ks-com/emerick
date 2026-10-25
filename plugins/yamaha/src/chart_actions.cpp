#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/yamaha/chart.hpp"
#include "registration.hpp"

namespace emerick::yamaha {

namespace {

using app::AppError;
using app::Json;

struct EventKind {
    const char* actionPrefix;
    const char* trackKind;
    const char* label;
    const char* contextMenu;
};

constexpr EventKind kChordEvents{"yamaha.chord", kChordKind, "chord", kChordContextMenu};
constexpr EventKind kSectionEvents{"yamaha.section", kStyleKind, "section", kSectionContextMenu};

model::TrackId kindTrack(const app::ProjectController& controller, const Json& params, const EventKind& kind) {
    const model::TrackId id = app::trackParam(controller.project(), params);
    const model::Track* track = controller.project().track(id);
    if (track->kind != kind.trackKind) {
        throw AppError(track->name + " is a " + track->kind + " track, pass a " + kind.trackKind + " track");
    }
    return id;
}

core::Ticks positionOf(const app::ProjectController& controller, const Json& params, const std::string& key) {
    const core::Ticks ticks = app::positionParam(params, key, controller.project().document().tempo);
    if (ticks < 0) {
        throw AppError("param " + key + " must not be before the start");
    }
    return ticks;
}

Chart trackChart(const model::Track& track) {
    return track.kind == kChordKind ? Chart{eventsOf<ChordEvent>(track.data), {}}
                                    : Chart{{}, eventsOf<SectionEvent>(track.data)};
}

Json chartState(const app::ProjectController& controller, const model::TrackId& id) {
    const model::Track& track = *controller.project().track(id);
    return {{"track", id.value},
            {"data", track.data.is_null() ? Json::array() : track.data},
            {"chart", chartText(trackChart(track), controller.project().document().tempo)}};
}

// A section that already has a marker of its name at its position keeps that one.
Json addSectionMarkers(app::ProjectController& controller, const model::Track& track) {
    std::vector<model::Marker> markers = controller.project().document().markers;
    int added = 0;
    for (const SectionEvent& event : eventsOf<SectionEvent>(track.data)) {
        const std::string name = sectionName(event);
        if (std::ranges::none_of(
                markers, [&](const model::Marker& marker) { return marker.at == event.at && marker.name == name; })) {
            markers.push_back({.name = name, .at = event.at, .color = std::nullopt});
            ++added;
        }
    }
    if (added > 0) {
        controller.project().setMarkers(std::move(markers), "add markers from " + track.name);
    }
    return {{"added", added}, {"markers", app::markersState(controller.project().document())}};
}

template <class Event>
Json editEvents(app::ProjectController& controller, const model::TrackId& id, std::string name,
                const std::function<void(std::vector<Event>&)>& edit) {
    std::vector<Event> events = eventsOf<Event>(controller.project().track(id)->data);
    edit(events);
    std::ranges::stable_sort(events, {}, &Event::at);
    controller.project().editTrack(id, std::move(name), [&](model::Track& track) { track.data = events; });
    return chartState(controller, id);
}

template <class Event>
void placeAt(std::vector<Event>& events, Event event) {
    std::erase_if(events, [&](const Event& existing) { return existing.at == event.at; });
    events.push_back(std::move(event));
}

template <class Event>
auto eventAt(std::vector<Event>& events, core::Ticks at, const EventKind& kind, const core::TempoMap& tempo) {
    const auto found = std::ranges::find(events, at, &Event::at);
    if (found == events.end()) {
        throw AppError(std::string{"no "} + kind.label + " at " + tempo.toBarBeatTick(at).format() +
                       ", run yamaha.chart.text for the positions");
    }
    return found;
}

template <class Event>
void registerBlockActions(app::Registry& registry, app::ProjectController& controller, app::Nav& nav,
                          const EventKind& kind) {
    const std::string label = kind.label;
    registry.addAction({.id = std::string{kind.actionPrefix} + ".edit",
                        .title = "Change " + label,
                        .aliases = {"edit " + label, "pick " + label},
                        .category = "Yamaha",
                        .schema = {{"required", {"track", "at"}}},
                        .run =
                            [&controller, &nav, &kind](const Json& params) {
                                const model::TrackId id = kindTrack(controller, params, kind);
                                const core::Ticks at = positionOf(controller, params, "at");
                                nav.setEditingBlock({{"track", app::qs(id.value)}, {"at", QVariant::fromValue(at)}});
                                return Json{{"track", id.value}, {"at", at}};
                            },
                        .menus = {kind.contextMenu}});
    registry.addAction({.id = std::string{kind.actionPrefix} + ".split",
                        .title = "Split " + label,
                        .aliases = {"cut " + label},
                        .category = "Yamaha",
                        .schema = {{"required", {"track", "at", "to"}}},
                        .run =
                            [&controller, &kind](const Json& params) {
                                const model::TrackId id = kindTrack(controller, params, kind);
                                const core::Ticks at = positionOf(controller, params, "at");
                                const core::Ticks to = positionOf(controller, params, "to");
                                const auto& tempo = controller.project().document().tempo;
                                return editEvents<Event>(controller, id, std::string{"split "} + kind.label,
                                                         [&](std::vector<Event>& events) {
                                                             const auto found = eventAt(events, at, kind, tempo);
                                                             const auto next = std::next(found);
                                                             if (to <= at || (next != events.end() && to >= next->at)) {
                                                                 throw AppError(std::string{"split inside the "} +
                                                                                kind.label + " at " +
                                                                                tempo.toBarBeatTick(at).format());
                                                             }
                                                             Event copy = *found;
                                                             copy.at = to;
                                                             events.push_back(std::move(copy));
                                                         });
                            },
                        .menus = {kind.contextMenu}});
    registry.addAction({.id = std::string{kind.actionPrefix} + ".move",
                        .title = "Move " + label,
                        .category = "Yamaha",
                        .schema = {{"required", {"track", "at", "to"}}},
                        .run = [&controller, &kind](const Json& params) {
                            const model::TrackId id = kindTrack(controller, params, kind);
                            const core::Ticks at = positionOf(controller, params, "at");
                            const core::Ticks to = positionOf(controller, params, "to");
                            const auto& tempo = controller.project().document().tempo;
                            return editEvents<Event>(
                                controller, id, std::string{"move "} + kind.label, [&](std::vector<Event>& events) {
                                    Event moved = *eventAt(events, at, kind, tempo);
                                    std::erase_if(events, [&](const Event& event) { return event.at == at; });
                                    moved.at = to;
                                    placeAt(events, std::move(moved));
                                });
                        }});
    registry.addAction({.id = std::string{kind.actionPrefix} + ".delete",
                        .title = "Delete " + label,
                        .aliases = {"remove " + label},
                        .category = "Yamaha",
                        .schema = {{"required", {"track", "at"}}},
                        .run =
                            [&controller, &kind](const Json& params) {
                                const model::TrackId id = kindTrack(controller, params, kind);
                                const core::Ticks at = positionOf(controller, params, "at");
                                const auto& tempo = controller.project().document().tempo;
                                return editEvents<Event>(controller, id, std::string{"delete "} + kind.label,
                                                         [&](std::vector<Event>& events) {
                                                             events.erase(eventAt(events, at, kind, tempo));
                                                         });
                            },
                        .menus = {kind.contextMenu}});
}

Chord chordParam(const Json& params) {
    const auto name = app::param<std::string>(params, "chord");
    const auto chord = Chord::parse(name);
    if (!chord) {
        throw AppError("unknown chord " + name +
                       ", write a root, a type and an optional bass such as C, F#m7, Bbmaj7/D, Gsus4 or N.C.");
    }
    return *chord;
}

std::optional<SectionEvent> sectionNamed(const std::string& name) {
    for (const SectionInfo& info : kSections) {
        for (int variation = 0; variation < variationCount(info); ++variation) {
            const SectionEvent event{0, info.kind, variation};
            if (sectionName(event) == name) {
                return event;
            }
        }
    }
    return std::nullopt;
}

// A section comes as a display name ("Main B", "Intro 2") or as a kind and a variation counted from 0.
SectionEvent sectionParam(const Json& params) {
    if (const auto name = app::optionalParam<std::string>(params, "name")) {
        if (const auto event = sectionNamed(*name)) {
            return *event;
        }
        throw AppError("unknown section " + *name + ", write a name such as Main A, Fill, Break, Intro 1 or Ending 2");
    }
    const auto kind = app::param<std::string>(params, "kind");
    const auto* const info = std::ranges::find(kSections, kind, &SectionInfo::json);
    if (info == kSections.end()) {
        throw AppError("unknown section kind " + kind + ", use main, fill, fillTo, break, intro or ending");
    }
    const int variation = app::optionalParam<int>(params, "variation").value_or(0);
    if (variation < 0 || variation >= variationCount(*info)) {
        throw AppError("variation for " + kind + " must be 0 to " + std::to_string(variationCount(*info) - 1));
    }
    return {0, info->kind, variation};
}

}  // namespace

void registerChartActions(app::Registry& registry, app::ProjectController& controller, app::Nav& nav) {
    registry.addAction(
        {.id = "yamaha.chord.set",
         .title = "Set chord",
         .aliases = {"add chord", "change chord"},
         .category = "Yamaha",
         .schema = {{"required", {"track", "at", "chord"}}, {"properties", {{"chord", {{"type", "string"}}}}}},
         .run = [&controller](const Json& params) {
             const model::TrackId id = kindTrack(controller, params, kChordEvents);
             const ChordEvent event{positionOf(controller, params, "at"), chordParam(params)};
             return editEvents<ChordEvent>(controller, id, "set chord " + event.chord.name(),
                                           [&](auto& events) { placeAt(events, event); });
         }});
    registerBlockActions<ChordEvent>(registry, controller, nav, kChordEvents);

    registry.addAction({.id = "yamaha.section.set",
                        .title = "Set style section",
                        .aliases = {"add section", "change section"},
                        .keywords = {"main", "fill", "break", "intro", "ending"},
                        .category = "Yamaha",
                        .schema = {{"required", {"track", "at"}},
                                   {"properties",
                                    {{"kind", {{"type", "string"}}},
                                     {"variation", {{"type", "integer"}}},
                                     {"name", {{"type", "string"}}}}}},
                        .run = [&controller](const Json& params) {
                            const model::TrackId id = kindTrack(controller, params, kSectionEvents);
                            SectionEvent event = sectionParam(params);
                            event.at = positionOf(controller, params, "at");
                            return editEvents<SectionEvent>(controller, id, "set section " + sectionName(event),
                                                            [&](auto& events) { placeAt(events, event); });
                        }});
    registerBlockActions<SectionEvent>(registry, controller, nav, kSectionEvents);
    registry.addAction(
        {.id = "yamaha.section.setBars",
         .title = "Set pattern length",
         .aliases = {"pattern bars", "loop length"},
         .category = "Yamaha",
         .schema = {{"required", {"track", "at", "bars"}}, {"properties", {{"bars", {{"type", "integer"}}}}}},
         .run =
             [&controller](const Json& params) {
                 const model::TrackId id = kindTrack(controller, params, kSectionEvents);
                 const core::Ticks at = positionOf(controller, params, "at");
                 const int bars = app::param<int>(params, "bars");
                 if (bars < 1) {
                     throw AppError("bars must be 1 or more");
                 }
                 const auto& tempo = controller.project().document().tempo;
                 return editEvents<SectionEvent>(controller, id, "set pattern length", [&](auto& events) {
                     const auto section = eventAt(events, at, kSectionEvents, tempo);
                     if (section->kind != Section::Main) {
                         throw AppError("only a Main repeats a pattern, pick a Main block");
                     }
                     section->bars = bars;
                 });
             },
         .menus = {kSectionContextMenu},
         .submenu =
             [] {
                 return std::array{1, 2, 4, 8} | std::views::transform([](int bars) {
                            return app::MenuChoice{.id = std::to_string(bars),
                                                   .title = std::to_string(bars) + (bars == 1 ? " bar" : " bars"),
                                                   .params = {{"bars", bars}}};
                        }) |
                        std::ranges::to<std::vector>();
             }});
    registry.addAction({.id = "yamaha.section.addMarkers",
                        .title = "Add markers from style sections",
                        .aliases = {"sections to markers", "mark sections"},
                        .category = "Yamaha",
                        .schema = {{"properties", {{"track", {{"type", "string"}}}}}},
                        .run =
                            [&controller](const Json& params) {
                                if (params.contains("track")) {
                                    const model::TrackId id = kindTrack(controller, params, kSectionEvents);
                                    return addSectionMarkers(controller, *controller.project().track(id));
                                }
                                const auto& tracks = controller.project().document().tracks;
                                const auto style =
                                    std::ranges::find(tracks, std::string{kStyleKind}, &model::Track::kind);
                                if (style == tracks.end()) {
                                    throw AppError("add a Style track first, or pass track");
                                }
                                return addSectionMarkers(controller, *style);
                            },
                        .menus = {kYamahaMenu}});

    registry.addAction({.id = "yamaha.chart.text",
                        .title = "Show chord chart",
                        .aliases = {"chart text", "lead sheet"},
                        .category = "Yamaha",
                        .schema = {{"required", {"track"}}},
                        .run = [&controller](const Json& params) {
                            const model::TrackId id = app::trackParam(controller.project(), params);
                            const std::string& kind = controller.project().track(id)->kind;
                            if (kind != kChordKind && kind != kStyleKind) {
                                throw AppError("pass a Chords or Style track");
                            }
                            return chartState(controller, id);
                        }});
}

}  // namespace emerick::yamaha
