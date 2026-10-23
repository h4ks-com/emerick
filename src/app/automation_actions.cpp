#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <ranges>
#include <set>
#include <string>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/automation_params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/model/automation.hpp"

namespace emerick::app {

namespace {

constexpr const char* kCategory = "Automation";

struct LaneRef {
    model::TrackId track;
    AutomationParam param;
};

// The param names the lane, or the lane the track shows when params names none.
LaneRef targetLane(const Registry& registry, const ProjectController& controller, const Nav& nav, const Json& params) {
    const model::TrackId track = targetTrack(controller, nav, params);
    const model::Track& found = *controller.project().track(track);
    const std::string id =
        optionalParam<std::string>(params, "param").value_or(nav.automationLane(qs(track.value)).toStdString());
    auto param = automationParam(registry, controller.project().document(), found, id);
    if (!param) {
        const auto ids = automationParams(registry, controller.project().document(), found) |
                         std::views::transform(&AutomationParam::id) | std::views::join_with(std::string{", "}) |
                         std::ranges::to<std::string>();
        throw AppError("track " + found.name + " cannot automate " + id +
                       (ids.empty() ? ", since it plays no audio" : ", pick one of " + ids));
    }
    return {track, std::move(*param)};
}

double checkedValue(const AutomationParam& param, double value) {
    if (std::isnan(value) || value < param.minimum || value > param.maximum) {
        throw AppError("value must be from " + std::to_string(param.minimum) + " to " + std::to_string(param.maximum) +
                       " for " + param.title);
    }
    return param.toggle ? std::round(value) : value;
}

double clampedValue(const AutomationParam& param, double value) {
    const double clamped = std::clamp(value, param.minimum, param.maximum);
    return param.toggle ? std::round(clamped) : clamped;
}

Json pointState(const model::AutomationPoint& point, const core::TempoMap& tempo) {
    return {{"id", point.id.value},
            {"tick", point.tick},
            {"position", tempo.toBarBeatTick(point.tick).format()},
            {"value", point.value},
            {"curve", core::automationCurveId(point.curve)}};
}

Json laneResult(const ProjectController& controller, const model::TrackId& track, const std::string& param) {
    const model::Track& found = *controller.project().track(track);
    const model::AutomationLane* lane = model::findLane(found, param);
    const core::TempoMap& tempo = controller.project().document().tempo;
    Json points = Json::array();
    if (lane != nullptr) {
        for (const model::AutomationPoint& point : lane->points) {
            points.push_back(pointState(point, tempo));
        }
    }
    return {{"track", track.value},
            {"param", param},
            {"mode", model::automationModeId(found.automationMode)},
            {"points", std::move(points)}};
}

model::AutomationLane& laneOf(model::Track& track, const std::string& param) {
    if (model::AutomationLane* lane = model::findLane(track, param)) {
        return *lane;
    }
    track.automation.push_back({.param = param, .points = {}});
    return track.automation.back();
}

// The points params name, or the selected points; either way each must exist in some lane.
std::set<std::string> targetPoints(const ProjectController& controller, const Nav& nav, const Json& params) {
    const auto names = optionalParam<std::vector<std::string>>(params, "points");
    std::set<std::string> ids;
    if (names) {
        ids.insert(names->begin(), names->end());
    } else {
        for (const QString& id : nav.selectedPoints()) {
            ids.insert(id.toStdString());
        }
    }
    std::set<std::string> found;
    for (const model::Track& track : controller.project().document().tracks) {
        for (const model::AutomationLane& lane : track.automation) {
            for (const model::AutomationPoint& point : lane.points) {
                if (ids.contains(point.id.value)) {
                    found.insert(point.id.value);
                }
            }
        }
    }
    if (const auto missing = std::ranges::find_if(ids, [&](const std::string& id) { return !found.contains(id); });
        names && missing != ids.end()) {
        throw AppError("no automation point " + *missing);
    }
    if (found.empty()) {
        throw AppError("select automation points first, or pass points");
    }
    return found;
}

// Edits every lane that holds one of the points as one undo step; edit gets each lane with its parameter.
Json editPoints(const Registry& registry, ProjectController& controller, const std::set<std::string>& ids,
                const std::string& name,
                const std::function<void(model::AutomationLane&, const AutomationParam&)>& edit) {
    model::Project& project = controller.project();
    const auto holds = [&](const model::AutomationLane& lane) {
        return std::ranges::any_of(lane.points,
                                   [&](const model::AutomationPoint& point) { return ids.contains(point.id.value); });
    };
    Json lanes = Json::array();
    project.group(name, [&] {
        for (const model::Track& track : std::vector(project.document().tracks)) {
            if (!std::ranges::any_of(track.automation, holds)) {
                continue;
            }
            project.editTrack(track.id, name, [&](model::Track& edited) {
                for (model::AutomationLane& lane : edited.automation) {
                    if (!holds(lane)) {
                        continue;
                    }
                    edit(lane, automationParam(registry, project.document(), track, lane.param)
                                   .value_or(AutomationParam{.id = lane.param,
                                                             .title = lane.param,
                                                             .minimum = std::numeric_limits<double>::lowest(),
                                                             .maximum = std::numeric_limits<double>::max(),
                                                             .value = 0.0,
                                                             .toggle = false,
                                                             .target = engine::AutomationTarget::gain,
                                                             .bus = {},
                                                             .unit = {},
                                                             .insert = {},
                                                             .insertParam = 0}));
                    model::sortPoints(lane);
                }
            });
            for (const model::AutomationLane& lane : project.track(track.id)->automation) {
                lanes.push_back(laneResult(controller, track.id, lane.param));
            }
        }
    });
    return {{"lanes", lanes}};
}

void registerPointEdits(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addAction(
        {.id = "automation.movePoints",
         .title = "Move automation points",
         .description = "Moves the points, by default the selected ones, by ticks and value, or to tick and value.",
         .aliases = {"drag automation"},
         .category = kCategory,
         .schema = {{"properties",
                     {{"points", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                      {"by", {{"type", "integer"}}},
                      {"delta", {{"type", "number"}}},
                      {"tick", {{"type", "integer"}}},
                      {"value", {{"type", "number"}}}}}},
         .run = [&registry, &controller, &nav](const Json& params) {
             const auto ids = targetPoints(controller, nav, params);
             const auto by = optionalParam<core::Ticks>(params, "by").value_or(0);
             const auto delta = optionalParam<double>(params, "delta").value_or(0.0);
             const auto tick = optionalParam<core::Ticks>(params, "tick");
             const auto value = optionalParam<double>(params, "value");
             return editPoints(registry, controller, ids, "move automation points",
                               [&](model::AutomationLane& lane, const AutomationParam& param) {
                                   for (model::AutomationPoint& point : lane.points) {
                                       if (ids.contains(point.id.value)) {
                                           point.tick = std::max<core::Ticks>(tick.value_or(point.tick + by), 0);
                                           point.value = clampedValue(param, value.value_or(point.value + delta));
                                       }
                                   }
                               });
         }});
    registry.addAction({.id = "automation.deletePoints",
                        .title = "Delete automation points",
                        .description = "Deletes the points, by default the selected ones.",
                        .aliases = {"remove automation points"},
                        .category = kCategory,
                        .schema = {{"properties", {{"points", {{"type", "array"}, {"items", {{"type", "string"}}}}}}}},
                        .run = [&registry, &controller, &nav](const Json& params) {
                            const auto ids = targetPoints(controller, nav, params);
                            Json result =
                                editPoints(registry, controller, ids, "delete automation points",
                                           [&](model::AutomationLane& lane, const AutomationParam&) {
                                               std::erase_if(lane.points, [&](const model::AutomationPoint& point) {
                                                   return ids.contains(point.id.value);
                                               });
                                           });
                            nav.setSelectedPoints({});
                            return result;
                        }});
    registry.addAction(
        {.id = "automation.setCurve",
         .title = "Set automation curve",
         .description = "Sets the curve from each point, by default the selected ones, to the next: linear, hold or "
                        "smooth.",
         .aliases = {"automation shape", "step automation"},
         .category = kCategory,
         .schema = {{"required", {"curve"}},
                    {"properties",
                     {{"points", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                      {"curve", {{"type", "string"}, {"enum", {"linear", "hold", "smooth"}}}}}}},
         .run =
             [&registry, &controller, &nav](const Json& params) {
                 const auto ids = targetPoints(controller, nav, params);
                 const auto id = param<std::string>(params, "curve");
                 const auto curve = core::automationCurveFromId(id);
                 if (!curve) {
                     throw AppError("curve must be linear, hold or smooth, not " + id);
                 }
                 return editPoints(registry, controller, ids, "set automation curve",
                                   [&](model::AutomationLane& lane, const AutomationParam&) {
                                       for (model::AutomationPoint& point : lane.points) {
                                           if (ids.contains(point.id.value)) {
                                               point.curve = *curve;
                                           }
                                       }
                                   });
             },
         .submenu =
             [] {
                 return core::kAutomationCurves | std::views::transform([](const core::AutomationCurveName& curve) {
                            return MenuChoice{.id = std::string{curve.id},
                                              .title = std::string{curve.id},
                                              .params = {{"curve", curve.id}}};
                        }) |
                        std::ranges::to<std::vector>();
             }});
    registry.addAction({.id = "automation.selectPoints",
                        .title = "Select automation points",
                        .aliases = {"pick automation points"},
                        .category = kCategory,
                        .schema = {{"required", {"points"}},
                                   {"properties", {{"points", {{"type", "array"}, {"items", {{"type", "string"}}}}}}}},
                        .run = [&controller, &nav](const Json& params) {
                            const auto names = param<std::vector<std::string>>(params, "points");
                            const auto ids =
                                names.empty() ? std::set<std::string>{} : targetPoints(controller, nav, params);
                            nav.setSelectedPoints(ids | std::views::transform(qs) | std::ranges::to<QStringList>());
                            return Json{{"selectedPoints", names}};
                        }});
}

void registerLaneActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addAction(
        {.id = "automation.addPoint",
         .title = "Add automation point",
         .description = "Adds a point at tick with value to the lane of param, by default the lane the track shows. "
                        "Gain and send levels are in dB, pan runs from -1 to 1 and mute is 0 or 1.",
         .aliases = {"automation node", "draw automation"},
         .category = kCategory,
         .schema = {{"required", {"tick", "value"}},
                    {"properties",
                     {{"track", {{"type", "string"}}},
                      {"param", {{"type", "string"}}},
                      {"tick", {{"type", "integer"}, {"minimum", 0}}},
                      {"value", {{"type", "number"}}},
                      {"curve", {{"type", "string"}, {"enum", {"linear", "hold", "smooth"}}}}}}},
         .run = [&registry, &controller, &nav](const Json& params) {
             const LaneRef lane = targetLane(registry, controller, nav, params);
             const core::Ticks tick = positionParam(params, "tick", controller.project().document().tempo);
             const double value = checkedValue(lane.param, param<double>(params, "value"));
             const std::string curveId = optionalParam<std::string>(params, "curve").value_or("linear");
             const auto curve = core::automationCurveFromId(curveId);
             if (!curve) {
                 throw AppError("curve must be linear, hold or smooth, not " + curveId);
             }
             const model::AutomationPoint point{
                 .id = model::AutomationPointId::make(), .tick = tick, .value = value, .curve = *curve};
             editTrack(controller, lane.track, "add automation point", [&](model::Track& edited) {
                 model::AutomationLane& edit = laneOf(edited, lane.param.id);
                 edit.points.push_back(point);
                 model::sortPoints(edit);
             });
             nav.setSelectedPoints({qs(point.id.value)});
             Json result = laneResult(controller, lane.track, lane.param.id);
             result["point"] = point.id.value;
             return result;
         }});
    registry.addAction({.id = "automation.clearLane",
                        .title = "Clear automation lane",
                        .description = "Deletes every point of the lane of param, by default the lane the track shows.",
                        .aliases = {"delete automation", "remove automation"},
                        .category = kCategory,
                        .schema = {{"properties", {{"track", {{"type", "string"}}}, {"param", {{"type", "string"}}}}}},
                        .run =
                            [&registry, &controller, &nav](const Json& params) {
                                const LaneRef lane = targetLane(registry, controller, nav, params);
                                editTrack(controller, lane.track, "clear automation lane", [&](model::Track& edited) {
                                    std::erase_if(edited.automation, [&](const model::AutomationLane& each) {
                                        return each.param == lane.param.id;
                                    });
                                });
                                return laneResult(controller, lane.track, lane.param.id);
                            },
                        .menus = {kTrackContextMenu}});
    registry.addAction({.id = "automation.showLane",
                        .title = "Show automation lane",
                        .description = "Shows the lane of param on the track and opens the automation view.",
                        .aliases = {"automation parameter"},
                        .category = kCategory,
                        .schema = {{"required", {"param"}},
                                   {"properties", {{"track", {{"type", "string"}}}, {"param", {{"type", "string"}}}}}},
                        .run = [&registry, &controller, &nav](const Json& params) {
                            const LaneRef lane = targetLane(registry, controller, nav, params);
                            QVariantMap lanes = nav.automationLanes();
                            lanes.insert(qs(lane.track.value), qs(lane.param.id));
                            nav.setAutomationLanes(lanes);
                            nav.setAutomationOpen(true);
                            return laneResult(controller, lane.track, lane.param.id);
                        }});
    registry.addAction({.id = "automation.getLane",
                        .title = "Get automation lane",
                        .description =
                            "Returns the lane of param with its points, by default the lane the track shows; without "
                            "includePoints only the point count.",
                        .category = kCategory,
                        .schema = {{"properties",
                                    {{"track", {{"type", "string"}}},
                                     {"param", {{"type", "string"}}},
                                     {"includePoints", {{"type", "boolean"}}}}}},
                        .run = [&registry, &controller, &nav](const Json& params) {
                            const LaneRef lane = targetLane(registry, controller, nav, params);
                            Json result = laneResult(controller, lane.track, lane.param.id);
                            result["pointCount"] = result.at("points").size();
                            if (!optionalParam<bool>(params, "includePoints").value_or(true)) {
                                result.erase("points");
                            }
                            return result;
                        }});
}

void registerModeActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addAction(
        {.id = "automation.setMode",
         .title = "Set automation mode",
         .description = "Off ignores the track's lanes, Read plays them, Touch writes while a control is held during "
                        "playback and Latch keeps writing its last value until the transport stops.",
         .aliases = {"automation read", "automation touch", "automation latch"},
         .category = kCategory,
         .schema = {{"required", {"mode"}},
                    {"properties",
                     {{"track", {{"type", "string"}}},
                      {"mode", {{"type", "string"}, {"enum", {"off", "read", "touch", "latch"}}}}}}},
         .run =
             [&controller, &nav](const Json& params) {
                 const model::TrackId track = targetTrack(controller, nav, params);
                 const auto id = param<std::string>(params, "mode");
                 const auto mode = model::automationModeFromId(id);
                 if (!mode) {
                     throw AppError("mode must be off, read, touch or latch, not " + id);
                 }
                 return editTrack(controller, track, "set automation mode",
                                  [&](model::Track& edited) { edited.automationMode = *mode; });
             },
         .menus = {kTrackContextMenu},
         .submenu =
             [] {
                 return model::kAutomationModes | std::views::transform([](const model::AutomationModeName& mode) {
                            return MenuChoice{.id = std::string{mode.id},
                                              .title = std::string{mode.title},
                                              .params = {{"mode", mode.id}}};
                        }) |
                        std::ranges::to<std::vector>();
             }});
    registry.addAction({.id = "view.toggleAutomation",
                        .title = "Show or hide automation",
                        .aliases = {"automation view", "track automation"},
                        .category = "View",
                        .shortcut = "A",
                        .run =
                            [&nav](const Json&) {
                                nav.setAutomationOpen(!nav.automationOpen());
                                return Json{{"automationOpen", nav.automationOpen()}};
                            },
                        .menus = {kViewMenu}});
}

}  // namespace

void registerAutomationActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registerLaneActions(registry, controller, nav);
    registerPointEdits(registry, controller, nav);
    registerModeActions(registry, controller, nav);
}

}  // namespace emerick::app
