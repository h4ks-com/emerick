#include <QVariant>
#include <QVariantMap>
#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "block_edit.hpp"
#include "emerick/app/project_state.hpp"

namespace emerick::yamaha {

namespace {

using app::AppError;
using app::Json;

struct BlockRange {
    model::TrackId track;
    core::Ticks from = 0;
    core::Ticks to = 0;
};

// Copied blocks keep their positions from the start of the copied range, which lasts span ticks.
template <class Event>
struct BlockClip {
    std::vector<Event> events;
    core::Ticks span = 0;
};

Json rangeProperties() { return {{"track", {{"type", "string"}}}, {"from", Json::object()}, {"to", Json::object()}}; }

std::string pluralOf(const EventKind& kind) { return std::string{kind.label} + "s"; }

template <class Event>
std::vector<Event> eventsIn(const std::vector<Event>& events, core::Ticks from, core::Ticks to) {
    return events | std::views::filter([&](const Event& event) { return event.at >= from && event.at < to; }) |
           std::ranges::to<std::vector>();
}

template <class Event>
core::Ticks blockEnd(const std::vector<Event>& events, const Event& event) {
    const auto next = std::ranges::upper_bound(events, event.at, {}, &Event::at);
    return next == events.end() ? event.at + kLastBlockTicks : next->at;
}

Json selectionState(const app::Nav& nav) { return {{"selectedBlocks", nav.toJson().at("selectedBlocks")}}; }

// Selects the blocks that overlap from to to, as the tick range from the first one's start to the last one's end.
template <class Event>
void selectBlocks(app::Nav& nav, const app::ProjectController& controller, const model::TrackId& id, core::Ticks from,
                  core::Ticks to) {
    const std::vector<Event> events = eventsOf<Event>(controller.project().track(id)->data);
    const auto touched =
        events |
        std::views::filter([&](const Event& event) { return event.at < to && blockEnd(events, event) > from; }) |
        std::ranges::to<std::vector>();
    if (touched.empty()) {
        nav.setSelectedBlocks({});
        return;
    }
    nav.setSelectedBlocks({{"track", app::qs(id.value)},
                           {"from", QVariant::fromValue(touched.front().at)},
                           {"to", QVariant::fromValue(blockEnd(events, touched.back()))}});
}

// The range params from and to, else the track's selected blocks.
BlockRange rangeOf(const app::ProjectController& controller, const app::Nav& nav, const Json& params,
                   const EventKind& kind) {
    const model::TrackId id = kindTrack(controller, params, kind);
    if (params.contains("from")) {
        const core::Ticks from = positionOf(controller, params, "from");
        const core::Ticks to = positionOf(controller, params, "to");
        if (to <= from) {
            throw AppError("param to must be after from");
        }
        return {id, from, to};
    }
    const QVariantMap selected = nav.selectedBlocks();
    if (selected.value("track").toString().toStdString() != id.value) {
        throw AppError(std::string{"select "} + kind.label + "s on the track first, or pass from and to");
    }
    return {id, selected.value("from").toLongLong(), selected.value("to").toLongLong()};
}

template <class Event>
BlockClip<Event> clipOf(const app::ProjectController& controller, const BlockRange& range, const EventKind& kind) {
    BlockClip<Event> clip{
        eventsIn(eventsOf<Event>(controller.project().track(range.track)->data), range.from, range.to),
        range.to - range.from};
    if (clip.events.empty()) {
        const auto& tempo = controller.project().document().tempo;
        throw AppError(std::string{"no "} + kind.label + " starts from " + tempo.toBarBeatTick(range.from).format() +
                       " to " + tempo.toBarBeatTick(range.to).format());
    }
    for (Event& event : clip.events) {
        event.at -= range.from;
    }
    return clip;
}

// The pasted blocks replace the ones in their range, and the block that played at the range's end before keeps
// playing there, so the music after the paste stays as it was.
template <class Event>
void pasteInto(std::vector<Event>& events, const BlockClip<Event>& clip, core::Ticks at) {
    const core::Ticks end = at + clip.span;
    const auto after = std::ranges::upper_bound(events, end, {}, &Event::at);
    std::optional<Event> resume;
    if (after != events.begin() && std::prev(after)->at < end) {
        resume = *std::prev(after);
        resume->at = end;
    }
    std::erase_if(events, [&](const Event& event) { return event.at >= at && event.at < end; });
    for (Event event : clip.events) {
        event.at += at;
        events.push_back(std::move(event));
    }
    Event last = clip.events.back();
    last.at = end;
    if (resume && *resume != last) {
        events.push_back(std::move(*resume));
    }
}

template <class Event>
Json pasteClip(app::ProjectController& controller, app::Nav& nav, const model::TrackId& id,
               const BlockClip<Event>& clip, core::Ticks at, const std::string& name) {
    Json state =
        editEvents<Event>(controller, id, name, [&](std::vector<Event>& events) { pasteInto(events, clip, at); });
    selectBlocks<Event>(nav, controller, id, at + clip.events.front().at, at + clip.events.back().at + 1);
    return state;
}

template <class Event>
Json moveBlocks(app::ProjectController& controller, app::Nav& nav, const Json& params, const EventKind& kind) {
    const model::TrackId id = kindTrack(controller, params, kind);
    const core::Ticks at = positionOf(controller, params, "at");
    const core::Ticks to = positionOf(controller, params, "to");
    const auto& tempo = controller.project().document().tempo;
    if (!params.contains("end")) {
        return editEvents<Event>(controller, id, std::string{"move "} + kind.label, [&](std::vector<Event>& events) {
            Event moved = *eventAt(events, at, kind, tempo);
            std::erase_if(events, [&](const Event& event) { return event.at == at; });
            moved.at = to;
            placeAt(events, std::move(moved));
        });
    }
    const core::Ticks end = positionOf(controller, params, "end");
    std::vector<Event> moved;
    Json state =
        editEvents<Event>(controller, id, std::string{"move "} + kind.label + "s", [&](std::vector<Event>& events) {
            moved = eventsIn(events, at, end);
            if (moved.empty()) {
                throw AppError(std::string{"no "} + kind.label + " starts from " + tempo.toBarBeatTick(at).format() +
                               " to " + tempo.toBarBeatTick(end).format());
            }
            std::erase_if(events, [&](const Event& event) { return event.at >= at && event.at < end; });
            for (Event& event : moved) {
                event.at += to - at;
                placeAt(events, event);
            }
        });
    selectBlocks<Event>(nav, controller, id, moved.front().at, moved.back().at + 1);
    return state;
}

template <class Event>
Json deleteBlocks(app::ProjectController& controller, app::Nav& nav, const Json& params, const EventKind& kind) {
    const auto& tempo = controller.project().document().tempo;
    if (params.contains("at")) {
        const model::TrackId id = kindTrack(controller, params, kind);
        const core::Ticks at = positionOf(controller, params, "at");
        return editEvents<Event>(controller, id, std::string{"delete "} + kind.label,
                                 [&](std::vector<Event>& events) { events.erase(eventAt(events, at, kind, tempo)); });
    }
    const BlockRange range = rangeOf(controller, nav, params, kind);
    Json state = editEvents<Event>(
        controller, range.track, std::string{"delete "} + kind.label + "s", [&](std::vector<Event>& events) {
            if (std::erase_if(events,
                              [&](const Event& event) { return event.at >= range.from && event.at < range.to; }) == 0) {
                throw AppError(std::string{"no "} + kind.label + " to delete there");
            }
        });
    nav.setSelectedBlocks({});
    return state;
}

template <class Event>
void registerEditActions(app::Registry& registry, app::ProjectController& controller, app::Nav& nav,
                         const EventKind& kind) {
    const std::string prefix = kind.actionPrefix;
    const std::string label = kind.label;
    registry.addAction({.id = prefix + ".edit",
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
    registry.addAction({.id = prefix + ".split",
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
    registry.addAction({.id = prefix + ".move",
                        .title = "Move " + label,
                        .description = "Moves the " + label + " at at to to, or with end every " + label +
                                       " that starts from at up to end by the same distance",
                        .category = "Yamaha",
                        .schema = {{"required", {"track", "at", "to"}},
                                   {"properties",
                                    {{"track", {{"type", "string"}}},
                                     {"at", Json::object()},
                                     {"to", Json::object()},
                                     {"end", Json::object()}}}},
                        .run = [&controller, &nav, &kind](const Json& params) {
                            return moveBlocks<Event>(controller, nav, params, kind);
                        }});
    registry.addAction({.id = prefix + ".delete",
                        .title = "Delete " + label,
                        .description = "Deletes the " + label + " at at, else every " + label +
                                       " that starts from from up to to, else the selected " + label + "s",
                        .aliases = {"remove " + label},
                        .category = "Yamaha",
                        .schema = {{"required", {"track"}},
                                   {"properties",
                                    {{"track", {{"type", "string"}}},
                                     {"at", Json::object()},
                                     {"from", Json::object()},
                                     {"to", Json::object()}}}},
                        .run = [&controller, &nav, &kind](
                                   const Json& params) { return deleteBlocks<Event>(controller, nav, params, kind); },
                        .menus = {kind.contextMenu}});
}

template <class Event>
void registerSelectionActions(app::Registry& registry, app::ProjectController& controller, app::Nav& nav,
                              const EventKind& kind) {
    const std::string prefix = kind.actionPrefix;
    const std::string plural = pluralOf(kind);
    registry.addAction({.id = prefix + ".select",
                        .title = "Select " + plural,
                        .description = "Selects the " + plural + " that overlap from to to, or none without from",
                        .category = "Yamaha",
                        .schema = {{"required", {"track"}}, {"properties", rangeProperties()}},
                        .run = [&controller, &nav, &kind](const Json& params) {
                            const model::TrackId id = kindTrack(controller, params, kind);
                            if (!params.contains("from")) {
                                nav.setSelectedBlocks({});
                                return selectionState(nav);
                            }
                            const BlockRange range = rangeOf(controller, nav, params, kind);
                            selectBlocks<Event>(nav, controller, id, range.from, range.to);
                            return selectionState(nav);
                        }});
    // The copy and paste handlers share the clipboard, and the registry owns the handlers.
    auto clipboard = std::make_shared<BlockClip<Event>>();
    registry.addAction(
        {.id = prefix + ".copy",
         .title = "Copy " + plural,
         .description = "Copies the " + plural + " that start from from up to to, else the selected " + plural,
         .category = "Yamaha",
         .schema = {{"required", {"track"}}, {"properties", rangeProperties()}},
         .run =
             [&controller, &nav, &kind, clipboard](const Json& params) {
                 *clipboard = clipOf<Event>(controller, rangeOf(controller, nav, params, kind), kind);
                 return Json{{"copied", clipboard->events.size()}, {"span", clipboard->span}};
             },
         .menus = {kind.contextMenu}});
    registry.addAction(
        {.id = prefix + ".paste",
         .title = "Paste " + plural,
         .description = "Pastes the copied " + plural + " at at, or at the playhead, over the " + plural + " there",
         .category = "Yamaha",
         .schema = {{"required", {"track"}}, {"properties", {{"track", {{"type", "string"}}}, {"at", Json::object()}}}},
         .run =
             [&controller, &nav, &kind, clipboard](const Json& params) {
                 const model::TrackId id = kindTrack(controller, params, kind);
                 if (clipboard->events.empty()) {
                     throw AppError("copy " + pluralOf(kind) + " first");
                 }
                 const core::Ticks at =
                     params.contains("at") ? positionOf(controller, params, "at") : controller.positionTicks();
                 return pasteClip(controller, nav, id, *clipboard, at, "paste " + pluralOf(kind));
             },
         .menus = {kind.contextMenu}});
    registry.addAction({.id = prefix + ".duplicate",
                        .title = "Duplicate " + plural,
                        .description = "Repeats the " + plural + " that start from from up to to, else the selected " +
                                       plural + ", right after them",
                        .aliases = {"repeat " + plural},
                        .category = "Yamaha",
                        .schema = {{"required", {"track"}}, {"properties", rangeProperties()}},
                        .run =
                            [&controller, &nav, &kind](const Json& params) {
                                const BlockRange range = rangeOf(controller, nav, params, kind);
                                return pasteClip(controller, nav, range.track, clipOf<Event>(controller, range, kind),
                                                 range.to, "duplicate " + pluralOf(kind));
                            },
                        .menus = {kind.contextMenu}});
    registry.addAction({.id = prefix + ".loop",
                        .title = "Loop selected " + plural,
                        .description = "Sets the cycle to the " + plural + " from from to to, else to the selected " +
                                       plural + ", and turns it on",
                        .aliases = {"cycle " + plural, "loop " + plural},
                        .category = "Yamaha",
                        .schema = {{"required", {"track"}}, {"properties", rangeProperties()}},
                        .run =
                            [&controller, &nav, &kind](const Json& params) {
                                const BlockRange range = rangeOf(controller, nav, params, kind);
                                controller.project().setCycle({.enabled = true, .start = range.from, .end = range.to},
                                                              "loop " + pluralOf(kind));
                                return Json{{"cycle", app::cycleState(controller.project().document())}};
                            },
                        .menus = {kind.contextMenu}});
}

}  // namespace

void registerChordBlockActions(app::Registry& registry, app::ProjectController& controller, app::Nav& nav) {
    registerEditActions<ChordEvent>(registry, controller, nav, kChordEvents);
    registerSelectionActions<ChordEvent>(registry, controller, nav, kChordEvents);
}

void registerSectionBlockActions(app::Registry& registry, app::ProjectController& controller, app::Nav& nav) {
    registerEditActions<SectionEvent>(registry, controller, nav, kSectionEvents);
    registerSelectionActions<SectionEvent>(registry, controller, nav, kSectionEvents);
}

}  // namespace emerick::yamaha
