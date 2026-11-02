#include "emerick/app/automation_params.hpp"

#include <algorithm>
#include <ranges>

#include "emerick/app/instruments.hpp"

namespace emerick::app {

namespace {

// Stepped params with more than two steps stay out, as choices do.
void addPluginParams(std::vector<AutomationParam>& params, const model::Insert& insert, const engine::Plugin& plugin) {
    for (const engine::PluginParam& described : plugin.params()) {
        const bool toggle = described.stepped && described.max - described.min == 1.0;
        if (described.stepped && !toggle) {
            continue;
        }
        params.push_back({.id = insert.id.value + "." + described.name,
                          .title = plugin.name() + " " + described.name,
                          .minimum = described.min,
                          .maximum = described.max,
                          .value = plugin.paramValue(described.id).value_or(described.defaultValue),
                          .toggle = toggle,
                          .target = engine::AutomationTarget::insert,
                          .bus = {},
                          .unit = {},
                          .insert = insert.id.value,
                          .insertParam = described.id,
                          .plugin = true});
    }
}

std::vector<AutomationParam> audibleParams(const Registry& registry, const model::Document& document,
                                           const model::Track& track) {
    const TrackType* type = registry.trackType(track.kind);
    return type != nullptr && playsAudio(*type, track) ? trackParams(document, track) : std::vector<AutomationParam>{};
}

std::optional<AutomationParam> paramIn(std::vector<AutomationParam> params, std::string_view id) {
    const auto found = std::ranges::find(params, id, &AutomationParam::id);
    return found == params.end() ? std::nullopt : std::optional{std::move(*found)};
}

engine::AutomationPoint enginePoint(const model::AutomationPoint& point, float value) {
    return {.tick = point.tick, .value = value, .curve = point.curve};
}

}  // namespace

std::vector<AutomationParam> automationParams(const Registry& registry, const model::Document& document,
                                              const model::Track& track, const PluginLookup& plugins) {
    std::vector<AutomationParam> params = audibleParams(registry, document, track);
    if (params.empty()) {
        return params;
    }
    for (const model::Insert& insert : track.inserts) {
        if (const auto plugin = insert.plugin ? plugins(insert.id) : nullptr) {
            addPluginParams(params, insert, *plugin);
        }
    }
    return params;
}

std::optional<AutomationParam> automationParam(const Registry& registry, const model::Document& document,
                                               const model::Track& track, const PluginLookup& plugins,
                                               std::string_view id) {
    return paramIn(automationParams(registry, document, track, plugins), id);
}

std::vector<engine::AutomationLane> pluginLanes(const model::Track& track, const PluginLookup& plugins) {
    if (track.automationMode == model::AutomationMode::off) {
        return {};
    }
    std::vector<engine::AutomationLane> lanes;
    for (const model::AutomationLane& lane : track.automation) {
        const auto dot = lane.param.find('.');
        const auto insert =
            std::ranges::find(track.inserts, model::InsertId{lane.param.substr(0, dot)}, &model::Insert::id);
        const auto plugin =
            dot != std::string::npos && insert != track.inserts.end() && insert->plugin ? plugins(insert->id) : nullptr;
        const auto param =
            plugin ? paramNamed(plugin->params(), std::string_view{lane.param}.substr(dot + 1)) : std::nullopt;
        if (!param || lane.points.empty()) {
            continue;
        }
        lanes.push_back({.target = engine::AutomationTarget::insert,
                         .bus = {},
                         .points = lane.points | std::views::transform([&](const model::AutomationPoint& point) {
                                       const double value = std::clamp(point.value, param->min, param->max);
                                       return enginePoint(point,
                                                          static_cast<float>(plugin->processValue(param->id, value)));
                                   }) |
                                   std::ranges::to<std::vector>(),
                         .insert = insert->id.value,
                         .param = param->id});
    }
    return lanes;
}

std::vector<engine::AutomationLane> engineLanes(const Registry& registry, const model::Document& document,
                                                const model::Track& track) {
    if (track.automationMode == model::AutomationMode::off) {
        return {};
    }
    std::vector<engine::AutomationLane> lanes;
    for (const model::AutomationLane& lane : track.automation) {
        const auto param = paramIn(audibleParams(registry, document, track), lane.param);
        if (!param || lane.points.empty()) {
            continue;
        }
        lanes.push_back({.target = param->target,
                         .bus = param->bus,
                         .points = lane.points | std::views::transform([](const model::AutomationPoint& point) {
                                       return enginePoint(point, static_cast<float>(point.value));
                                   }) |
                                   std::ranges::to<std::vector>(),
                         .insert = param->insert,
                         .param = param->insertParam});
    }
    return lanes;
}

}  // namespace emerick::app
