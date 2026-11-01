#include "emerick/app/automation_params.hpp"

#include <algorithm>
#include <ranges>

#include "emerick/app/instruments.hpp"
#include "emerick/engine/effects.hpp"

namespace emerick::app {

namespace {

// Choice params, such as a delay's note value, stay out, because a lane moves a value between points.
void addInsertParams(std::vector<AutomationParam>& params, const model::Insert& insert) {
    const engine::EffectSpec* spec = engine::findEffect(insert.type);
    if (spec == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < spec->params.size(); ++index) {
        const engine::EffectParam& described = spec->params[index];
        if (described.kind == engine::ParamKind::choice) {
            continue;
        }
        const std::string id{described.id};
        const auto value = insert.params.find(id);
        params.push_back({.id = insert.id.value + "." + id,
                          .title = std::string{spec->title} + " " + std::string{described.title},
                          .minimum = described.minimum,
                          .maximum = described.maximum,
                          .value = value != insert.params.end() ? value->second : described.defaultValue,
                          .toggle = described.kind == engine::ParamKind::toggle,
                          .target = engine::AutomationTarget::insert,
                          .bus = {},
                          .unit = std::string{described.unit},
                          .insert = insert.id.value,
                          .insertParam = index});
    }
}

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

std::vector<AutomationParam> trackParams(const Registry& registry, const model::Document& document,
                                         const model::Track& track);

std::optional<AutomationParam> paramIn(std::vector<AutomationParam> params, std::string_view id) {
    const auto found = std::ranges::find(params, id, &AutomationParam::id);
    return found == params.end() ? std::nullopt : std::optional{std::move(*found)};
}

engine::AutomationPoint enginePoint(const model::AutomationPoint& point, float value) {
    return {.tick = point.tick, .value = value, .curve = point.curve};
}

}  // namespace

std::string sendParamId(const model::TrackId& bus) { return "send." + bus.value + ".level"; }

std::vector<AutomationParam> automationParams(const Registry& registry, const model::Document& document,
                                              const model::Track& track, const PluginLookup& plugins) {
    std::vector<AutomationParam> params = trackParams(registry, document, track);
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

namespace {

std::vector<AutomationParam> trackParams(const Registry& registry, const model::Document& document,
                                         const model::Track& track) {
    const TrackType* type = registry.trackType(track.kind);
    if (type == nullptr || !playsAudio(*type, track)) {
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
                                         .unit = "dB",
                                         .insert = {},
                                         .insertParam = 0},
                                        {.id = "pan",
                                         .title = "Pan",
                                         .minimum = -1.0,
                                         .maximum = 1.0,
                                         .value = track.pan,
                                         .toggle = false,
                                         .target = engine::AutomationTarget::pan,
                                         .bus = {},
                                         .unit = "pan",
                                         .insert = {},
                                         .insertParam = 0},
                                        {.id = "mute",
                                         .title = "Mute",
                                         .minimum = 0.0,
                                         .maximum = 1.0,
                                         .value = track.mute ? 1.0 : 0.0,
                                         .toggle = true,
                                         .target = engine::AutomationTarget::mute,
                                         .bus = {},
                                         .unit = "",
                                         .insert = {},
                                         .insertParam = 0}};
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
                          .unit = "dB",
                          .insert = {},
                          .insertParam = 0});
    }
    for (const model::Insert& insert : track.inserts) {
        addInsertParams(params, insert);
    }
    return params;
}

}  // namespace

std::vector<engine::AutomationLane> engineLanes(const Registry& registry, const model::Document& document,
                                                const model::Track& track) {
    if (track.automationMode == model::AutomationMode::off) {
        return {};
    }
    std::vector<engine::AutomationLane> lanes;
    for (const model::AutomationLane& lane : track.automation) {
        const auto param = paramIn(trackParams(registry, document, track), lane.param);
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
