#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/yamaha/chart.hpp"
#include "registration.hpp"

namespace emerick::yamaha {

struct EventKind {
    const char* actionPrefix;
    const char* trackKind;
    const char* label;
    const char* contextMenu;
};

inline constexpr EventKind kChordEvents{"yamaha.chord", kChordKind, "chord", kChordContextMenu};
inline constexpr EventKind kSectionEvents{"yamaha.section", kStyleKind, "section", kSectionContextMenu};

inline model::TrackId kindTrack(const app::ProjectController& controller, const app::Json& params,
                                const EventKind& kind) {
    const model::TrackId id = app::trackParam(controller.project(), params);
    const model::Track* track = controller.project().track(id);
    if (track->kind != kind.trackKind) {
        throw app::AppError(track->name + " is a " + track->kind + " track, pass a " + kind.trackKind + " track");
    }
    return id;
}

inline core::Ticks positionOf(const app::ProjectController& controller, const app::Json& params,
                              const std::string& key) {
    const core::Ticks ticks = app::positionParam(params, key, controller.project().document().tempo);
    if (ticks < 0) {
        throw app::AppError("param " + key + " must not be before the start");
    }
    return ticks;
}

inline Chart trackChart(const model::Track& track) {
    return track.kind == kChordKind ? Chart{eventsOf<ChordEvent>(track.data), {}}
                                    : Chart{{}, eventsOf<SectionEvent>(track.data)};
}

inline app::Json chartState(const app::ProjectController& controller, const model::TrackId& id) {
    const model::Track& track = *controller.project().track(id);
    return {{"track", id.value},
            {"data", track.data.is_null() ? app::Json::array() : track.data},
            {"chart", chartText(trackChart(track), controller.project().document().tempo)}};
}

template <class Event>
app::Json editEvents(app::ProjectController& controller, const model::TrackId& id, std::string name,
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
        throw app::AppError(std::string{"no "} + kind.label + " at " + tempo.toBarBeatTick(at).format() +
                            ", run yamaha.chart.text for the positions");
    }
    return found;
}

void registerChordBlockActions(app::Registry& registry, app::ProjectController& controller, app::Nav& nav);
void registerSectionBlockActions(app::Registry& registry, app::ProjectController& controller, app::Nav& nav);

}  // namespace emerick::yamaha
