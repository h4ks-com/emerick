#include "emerick/model/automation.hpp"

#include <algorithm>
#include <iterator>

#include "emerick/core/lookup.hpp"

namespace emerick::model {

std::optional<AutomationMode> automationModeFromId(std::string_view id) {
    const auto* found = core::findEntry(kAutomationModes, id, &AutomationModeName::id);
    return found == nullptr ? std::nullopt : std::optional{found->mode};
}

std::string_view automationModeId(AutomationMode mode) {
    return std::ranges::find(kAutomationModes, mode, &AutomationModeName::mode)->id;
}

AutomationLane* findLane(Track& track, std::string_view param) {
    const auto found = std::ranges::find(track.automation, param, &AutomationLane::param);
    return found == track.automation.end() ? nullptr : &*found;
}

const AutomationLane* findLane(const Track& track, std::string_view param) {
    const auto found = std::ranges::find(track.automation, param, &AutomationLane::param);
    return found == track.automation.end() ? nullptr : &*found;
}

std::optional<double> automationValueAt(const AutomationLane& lane, Ticks tick) {
    const auto& points = lane.points;
    if (points.empty()) {
        return std::nullopt;
    }
    const auto next = std::ranges::upper_bound(points, tick, {}, &AutomationPoint::tick);
    if (next == points.begin()) {
        return points.front().value;
    }
    if (next == points.end()) {
        return points.back().value;
    }
    const AutomationPoint& from = *std::prev(next);
    const double t = static_cast<double>(tick - from.tick) / static_cast<double>(next->tick - from.tick);
    return core::automationBetween(from.value, next->value, t, from.curve);
}

void sortPoints(AutomationLane& lane) { std::ranges::stable_sort(lane.points, {}, &AutomationPoint::tick); }

}  // namespace emerick::model
