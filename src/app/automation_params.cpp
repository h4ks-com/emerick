#include "emerick/app/automation_params.hpp"

#include <algorithm>
#include <ranges>

namespace emerick::app {

std::string sendParamId(const model::TrackId& bus) { return "send." + bus.value + ".level"; }

std::vector<AutomationParam> automationParams(const Registry& registry, const model::Document& document,
                                              const model::Track& track) {
    const TrackType* type = registry.trackType(track.kind);
    if (type == nullptr || !(type->routes.audio || type->routes.bus)) {
        return {};
    }
    std::vector<AutomationParam> params{{.id = "gain",
                                         .title = "Volume",
                                         .minimum = kAutomationGainMinDb,
                                         .maximum = kAutomationGainMaxDb,
                                         .value = track.gainDb,
                                         .toggle = false,
                                         .target = engine::AutomationTarget::gain,
                                         .bus = {},
                                         .unit = "dB"},
                                        {.id = "pan",
                                         .title = "Pan",
                                         .minimum = -1.0,
                                         .maximum = 1.0,
                                         .value = track.pan,
                                         .toggle = false,
                                         .target = engine::AutomationTarget::pan,
                                         .bus = {},
                                         .unit = "pan"},
                                        {.id = "mute",
                                         .title = "Mute",
                                         .minimum = 0.0,
                                         .maximum = 1.0,
                                         .value = track.mute ? 1.0 : 0.0,
                                         .toggle = true,
                                         .target = engine::AutomationTarget::mute,
                                         .bus = {},
                                         .unit = ""}};
    for (const model::Send& send : track.sends) {
        const model::Track* bus = findTrack(document, send.bus);
        params.push_back({.id = sendParamId(send.bus),
                          .title = (bus != nullptr ? bus->name : send.bus.value) + " send",
                          .minimum = kAutomationGainMinDb,
                          .maximum = kAutomationGainMaxDb,
                          .value = send.levelDb,
                          .toggle = false,
                          .target = engine::AutomationTarget::send,
                          .bus = send.bus.value,
                          .unit = "dB"});
    }
    return params;
}

std::optional<AutomationParam> automationParam(const Registry& registry, const model::Document& document,
                                               const model::Track& track, std::string_view id) {
    std::vector<AutomationParam> params = automationParams(registry, document, track);
    const auto found = std::ranges::find(params, id, &AutomationParam::id);
    return found == params.end() ? std::nullopt : std::optional{std::move(*found)};
}

std::vector<engine::AutomationLane> engineLanes(const Registry& registry, const model::Document& document,
                                                const model::Track& track) {
    if (track.automationMode == model::AutomationMode::off) {
        return {};
    }
    std::vector<engine::AutomationLane> lanes;
    for (const model::AutomationLane& lane : track.automation) {
        const auto param = automationParam(registry, document, track, lane.param);
        if (!param || lane.points.empty()) {
            continue;
        }
        lanes.push_back({.target = param->target,
                         .bus = param->bus,
                         .points = lane.points | std::views::transform([](const model::AutomationPoint& point) {
                                       return engine::AutomationPoint{.tick = point.tick,
                                                                      .value = static_cast<float>(point.value),
                                                                      .curve = point.curve};
                                   }) |
                                   std::ranges::to<std::vector>()});
    }
    return lanes;
}

}  // namespace emerick::app
