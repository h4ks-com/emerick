#include "emerick/app/lua_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
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
        const auto first = entry.begin();
        const engine::EffectSpec& spec = effectType(first.key());
        for (const auto& [param, value] : first.value().items()) {
            effectParamValue(effectParamNamed(spec, param), value);
        }
    }
    return list;
}

struct LanePoint {
    Json tick;
    double value;
    std::optional<Json> curve;
};

// A fractional beat becomes ticks at the document's ticks per quarter note.
std::string barBeatPosition(double bar, double beat) {
    constexpr double kLast = std::numeric_limits<int>::max();
    if (bar < 1 || beat < 1 || bar > kLast || beat > kLast || bar != std::floor(bar)) {
        throw AppError("give each point a whole bar from 1 and a beat from 1, such as {5, 2.5, 0.8}");
    }
    const double wholeBeat = std::floor(beat);
    return std::format("{}.{}.{}", static_cast<int>(bar), static_cast<int>(wholeBeat),
                       std::llround((beat - wholeBeat) * static_cast<double>(core::kTicksPerQuarter)));
}

std::optional<Json> namedField(const Json& point, const char* key) {
    const auto found = point.find(key);
    return found == point.end() ? std::nullopt : std::optional{*found};
}

std::optional<LanePoint> lanePoint(const Json& point) {
    if (point.is_array()) {
        if (point.size() < kCurve || point.size() > kCurve + 1 ||
            !std::ranges::all_of(point | std::views::take(kCurve), &Json::is_number)) {
            return std::nullopt;
        }
        return LanePoint{.tick = barBeatPosition(point.at(kBar), point.at(kBeat)),
                         .value = point.at(kValue),
                         .curve = point.size() > kCurve ? std::optional{point.at(kCurve)} : std::nullopt};
    }
    const auto value = point.is_object() ? namedField(point, "value") : std::nullopt;
    if (!value || !value->is_number()) {
        return std::nullopt;
    }
    if (const auto at = namedField(point, "at")) {
        return LanePoint{.tick = *at, .value = *value, .curve = namedField(point, "curve")};
    }
    const Json bar = point.value("bar", Json{});
    const Json beat = point.value("beat", Json(1));
    if (!bar.is_number() || !beat.is_number()) {
        return std::nullopt;
    }
    return LanePoint{.tick = barBeatPosition(bar, beat), .value = *value, .curve = namedField(point, "curve")};
}

Json pointParams(const std::string& track, const std::string& param, const Json& lane, const LanePoint& point) {
    if (std::isnan(point.value) || point.value < lane.at("minimum").get<double>() ||
        point.value > lane.at("maximum").get<double>()) {
        throw AppError(std::format("give each point of {} a value from {} to {}", param,
                                   lane.at("minimum").get<double>(), lane.at("maximum").get<double>()));
    }
    Json params{{"track", track}, {"param", param}, {"tick", point.tick}, {"value", point.value}};
    if (point.curve) {
        if (!point.curve->is_string() || !core::automationCurveFromId(point.curve->get<std::string>())) {
            throw AppError("give each point's curve as linear, hold or smooth");
        }
        params["curve"] = *point.curve;
    }
    return params;
}

}  // namespace

Json addBus(const Registry& registry, const std::string& name, const Json& inserts) {
    const Json list = insertList(inserts);
    const std::string bus = registry.run("track.addBus", {{"name", name}}, ActionSource::Lua).at("id");
    Json ids = Json::array();
    for (const Json& entry : list) {
        const auto first = entry.begin();
        const std::string insert =
            registry.run("insert.add", {{"track", bus}, {"type", first.key()}}, ActionSource::Lua).at("id");
        for (const auto& [param, value] : first.value().items()) {
            registry.run("insert.setParam", {{"insert", insert}, {"param", param}, {"value", value}},
                         ActionSource::Lua);
        }
        ids.push_back(insert);
    }
    return {{"bus", bus}, {"inserts", ids}};
}

Json automate(const Registry& registry, const std::string& track, const std::string& param, const Json& points) {
    const auto parsed = points.is_array() ? points | std::views::transform(lanePoint) | std::ranges::to<std::vector>()
                                          : std::vector<std::optional<LanePoint>>{std::nullopt};
    if (!std::ranges::all_of(parsed, [](const auto& point) { return point.has_value(); })) {
        throw AppError(
            "pass points as a list of {bar, beat, value, curve?} or {at = position, value = v, curve = c} "
            "tables");
    }
    const Json lane = registry.run("automation.getLane", {{"track", track}, {"param", param}, {"includePoints", false}},
                                   ActionSource::Lua);
    const std::vector<Json> calls = parsed | std::views::transform([&](const std::optional<LanePoint>& point) {
                                        return pointParams(track, param, lane, *point);
                                    }) |
                                    std::ranges::to<std::vector>();
    Json ids = Json::array();
    for (const Json& params : calls) {
        ids.push_back(registry.run("automation.addPoint", params, ActionSource::Lua).at("point"));
    }
    return {{"points", ids}};
}

}  // namespace emerick::app
