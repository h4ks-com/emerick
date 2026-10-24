#pragma once

#include <algorithm>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/yamaha/chart.hpp"

namespace emerick::yamaha {

inline constexpr const char* kChordKind = "yamaha.chord";
inline constexpr const char* kStyleKind = "yamaha.style";
inline constexpr const char* kYamahaMenu = "Plugins/Yamaha Arranger";
// The menus a right click on a chord or section block opens, which run their actions with that block.
inline constexpr const char* kChordContextMenu = "Chord context";
inline constexpr const char* kSectionContextMenu = "Section context";

inline constexpr const char* kChordChannelSetting = "yamaha.chordDetectChannel";
inline constexpr const char* kControllerPortSetting = "yamaha.controllerPort";
inline constexpr const char* kControllerChannelSetting = "yamaha.controllerChannel";
inline constexpr const char* kSnapSetting = "yamaha.snapToBeat";
inline constexpr const char* kAnticipationSetting = "yamaha.anticipation";
inline constexpr const char* kSendVoicesSetting = "yamaha.sendVoicesOnPlay";

// A Chords or Style track keeps its events in data as a JSON array; a new track has no data and no events.
template <class Event>
std::vector<Event> eventsOf(const nlohmann::json& data) {
    if (data.is_null()) {
        return {};
    }
    try {
        auto events = data.get<std::vector<Event>>();
        std::ranges::stable_sort(events, {}, &Event::at);
        return events;
    } catch (const nlohmann::json::exception& error) {
        throw app::AppError(std::string{"the track data is damaged: "} + error.what());
    } catch (const std::invalid_argument& error) {
        throw app::AppError(std::string{"the track data is damaged: "} + error.what());
    }
}

ChartOptions chartOptions(const app::Registry& registry, int shiftBars = 0);

void registerSettings(app::Registry& registry, app::ProjectController& controller, app::Nav& nav);
void registerTrackKinds(app::Registry& registry);
void registerChartActions(app::Registry& registry, app::ProjectController& controller, app::Nav& nav);
void registerArrangerActions(app::Registry& registry, app::ProjectController& controller);

}  // namespace emerick::yamaha
