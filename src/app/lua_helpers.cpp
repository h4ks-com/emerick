#include "emerick/app/lua_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>

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
    return list;
}

core::TempoMap projectTempo(const Registry& registry) {
    const Json tempo = registry.state("project").at("tempo");
    return {
        .bpm = tempo.at("bpm").get<double>(),
        .meter = {.numerator = tempo.at("numerator").get<int>(), .denominator = tempo.at("denominator").get<int>()}};
}

core::Ticks pointTick(const core::TempoMap& tempo, const Json& point) {
    const double bar = point.at(kBar);
    const double beat = point.at(kBeat);
    if (bar < 1 || beat < 1 || bar != std::floor(bar)) {
        throw AppError("give each point a whole bar from 1 and a beat from 1, such as {5, 2.5, 0.8}");
    }
    const double wholeBeat = std::floor(beat);
    return tempo.fromBarBeatTick({.bar = static_cast<int>(bar), .beat = static_cast<int>(wholeBeat), .tick = 0}) +
           std::llround((beat - wholeBeat) * static_cast<double>(tempo.ticksPerBeat()));
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
    const core::TempoMap tempo = projectTempo(registry);
    Json ids = Json::array();
    for (const Json& point : points) {
        Json params{{"track", track}, {"param", param}, {"tick", pointTick(tempo, point)}, {"value", point.at(kValue)}};
        if (point.size() > kCurve) {
            params["curve"] = point.at(kCurve);
        }
        ids.push_back(registry.run("automation.addPoint", params, ActionSource::Lua).at("point"));
    }
    return {{"points", ids}};
}

std::string trackIdByName(const Registry& registry, const std::string& name) {
    const Json tracks = registry.state("project").at("tracks");
    const auto found = std::ranges::find_if(tracks, [&](const Json& track) { return track.at("name") == name; });
    if (found == tracks.end()) {
        throw AppError("no track named " + name + ", read emerick.project() for the track names");
    }
    return found->at("id");
}

}  // namespace emerick::app
