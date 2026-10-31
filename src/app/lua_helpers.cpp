#include "emerick/app/lua_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/core/automation.hpp"
#include "emerick/core/time.hpp"

namespace emerick::app {

namespace {

constexpr std::size_t kBar = 0;
constexpr std::size_t kBeat = 1;
constexpr std::size_t kValue = 2;
constexpr std::size_t kCurve = 3;

// A Lua table with string keys has no order, so each insert comes as its own one-key table in a list.
Json insertList(const Json& inserts) {
    if (inserts.is_object() && inserts.empty()) {
        return Json::array();
    }
    const Json list = inserts.is_object() && inserts.size() == 1 ? Json::array({inserts}) : inserts;
    const bool valid = list.is_array() && std::ranges::all_of(list, [](const Json& entry) {
                           return entry.is_object() && entry.size() == 1 && entry.begin()->is_object();
                       });
    if (!valid) {
        throw AppError("pass inserts as a list of {type = params} tables, such as {{reverb = {mix = 0.3}}}");
    }
    for (const Json& entry : list) {
        const auto& [type, params] = *entry.items().begin();
        const engine::EffectSpec& spec = effectType(type);
        for (const auto& [param, value] : params.items()) {
            effectParamValue(effectParamNamed(spec, param), value);
        }
    }
    return list;
}

// A fractional beat becomes ticks at the document's ticks per quarter note.
std::string pointPosition(const Json& point) {
    const double bar = point.at(kBar);
    const double beat = point.at(kBeat);
    constexpr double kLast = std::numeric_limits<int>::max();
    if (bar < 1 || beat < 1 || bar > kLast || beat > kLast || bar != std::floor(bar)) {
        throw AppError("give each point a whole bar from 1 and a beat from 1, such as {5, 2.5, 0.8}");
    }
    const double wholeBeat = std::floor(beat);
    return std::format("{}.{}.{}", static_cast<int>(bar), static_cast<int>(wholeBeat),
                       std::llround((beat - wholeBeat) * static_cast<double>(core::kTicksPerQuarter)));
}

Json pointParams(const std::string& track, const std::string& param, const Json& lane, const Json& point) {
    const double value = point.at(kValue);
    if (std::isnan(value) || value < lane.at("minimum").get<double>() || value > lane.at("maximum").get<double>()) {
        throw AppError(std::format("give each point of {} a value from {} to {}", param,
                                   lane.at("minimum").get<double>(), lane.at("maximum").get<double>()));
    }
    Json params{{"track", track}, {"param", param}, {"tick", pointPosition(point)}, {"value", value}};
    if (point.size() > kCurve) {
        if (!point.at(kCurve).is_string() || !core::automationCurveFromId(point.at(kCurve).get<std::string>())) {
            throw AppError("give each point's curve as linear, hold or smooth");
        }
        params["curve"] = point.at(kCurve);
    }
    return params;
}

}  // namespace

Json addBus(const Registry& registry, const std::string& name, const Json& inserts) {
    const Json list = insertList(inserts);
    const std::string bus = registry.run("track.addBus", {{"name", name}}, ActionSource::Lua).at("id");
    Json ids = Json::array();
    for (const Json& entry : list) {
        const auto& [type, params] = *entry.items().begin();
        const std::string insert =
            registry.run("insert.add", {{"track", bus}, {"type", type}}, ActionSource::Lua).at("id");
        for (const auto& [param, value] : params.items()) {
            registry.run("insert.setParam", {{"insert", insert}, {"param", param}, {"value", value}},
                         ActionSource::Lua);
        }
        ids.push_back(insert);
    }
    return {{"bus", bus}, {"inserts", ids}};
}

Json automate(const Registry& registry, const std::string& track, const std::string& param, const Json& points) {
    if (!points.is_array() || !std::ranges::all_of(points, [](const Json& point) {
            return point.is_array() && point.size() >= kCurve && point.size() <= kCurve + 1 &&
                   std::ranges::all_of(point | std::views::take(kCurve), &Json::is_number);
        })) {
        throw AppError("pass points as a list of {bar, beat, value, curve?} tables");
    }
    const Json lane = registry.run("automation.getLane", {{"track", track}, {"param", param}, {"includePoints", false}},
                                   ActionSource::Lua);
    const std::vector<Json> calls =
        points | std::views::transform([&](const Json& point) { return pointParams(track, param, lane, point); }) |
        std::ranges::to<std::vector>();
    Json ids = Json::array();
    for (const Json& params : calls) {
        ids.push_back(registry.run("automation.addPoint", params, ActionSource::Lua).at("point"));
    }
    return {{"points", ids}};
}

}  // namespace emerick::app
