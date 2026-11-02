#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/note_edit.hpp"
#include "emerick/app/params.hpp"
#include "emerick/app/project_view.hpp"
#include "emerick/model/controls.hpp"
#include "emerick/model/midi_notes.hpp"

namespace emerick::app {

namespace {

constexpr const char* kLanesSetting = "midi.lanes";

std::string checkedLane(const std::string& lane) {
    if (lane != kVelocityLane && !model::parseControl(lane)) {
        throw AppError("lane must be velocity, pitchBend, pressure or cc0 to cc127, got " + lane);
    }
    return lane;
}

model::Control checkedControl(const std::string& lane) {
    const auto control = model::parseControl(lane);
    if (!control) {
        throw AppError("lane must be pitchBend, pressure or cc0 to cc127, got " + lane +
                       "; set velocity with midi.setVelocity");
    }
    return *control;
}

std::vector<std::string> shownLanes(const Registry& registry) {
    return registry.settingValue(kLanesSetting).get<std::vector<std::string>>();
}

Json setLanes(Registry& registry, std::vector<std::string> lanes) {
    std::vector<std::string> unique;
    for (std::string& lane : lanes) {
        if (!std::ranges::contains(unique, lane)) {
            unique.push_back(std::move(lane));
        }
    }
    return {{"lanes", registry.setSetting(kLanesSetting, unique)}};
}

Json addLane(Registry& registry, const Json& params) {
    std::vector<std::string> lanes = shownLanes(registry);
    const auto* const unshown = std::ranges::find_if(
        kStandardLanes, [&](const LaneName& name) { return !std::ranges::contains(lanes, name.id); });
    const auto fallback = unshown == kStandardLanes.end() ? kStandardLanes.front().id : unshown->id;
    lanes.push_back(checkedLane(optionalParam<std::string>(params, "lane").value_or(std::string{fallback})));
    return setLanes(registry, std::move(lanes));
}

Json removeLane(Registry& registry, const Json& params) {
    std::vector<std::string> lanes = shownLanes(registry);
    if (std::erase(lanes, param<std::string>(params, "lane")) == 0) {
        throw AppError("the piano roll shows no lane " + param<std::string>(params, "lane"));
    }
    if (lanes.empty()) {
        throw AppError("the piano roll keeps at least one controller lane");
    }
    return setLanes(registry, std::move(lanes));
}

Json changeLane(Registry& registry, const Json& params) {
    std::vector<std::string> lanes = shownLanes(registry);
    const auto lane = std::ranges::find(lanes, param<std::string>(params, "lane"));
    if (lane == lanes.end()) {
        throw AppError("the piano roll shows no lane " + param<std::string>(params, "lane"));
    }
    *lane = checkedLane(param<std::string>(params, "to"));
    return setLanes(registry, std::move(lanes));
}

model::ControlPoint checkedPoint(const Json& entry, model::Control control, const model::MidiClip& clip) {
    if (entry.is_array() &&
        (entry.size() != 2 || !entry.at(0).is_number_integer() || !entry.at(1).is_number_integer())) {
        throw AppError("give each point as {at, value} or [at, value], with whole numbers, got " + entry.dump());
    }
    const model::ControlPoint point =
        entry.is_array()
            ? model::ControlPoint{.at = entry.at(0).get<core::Ticks>(), .value = entry.at(1).get<int>()}
            : model::ControlPoint{.at = param<core::Ticks>(entry, "at"), .value = param<int>(entry, "value")};
    if (point.at < 0 || point.at >= clip.length) {
        throw AppError("at must be inside the clip, from 0 to " + std::to_string(clip.length - 1) + " ticks");
    }
    if (point.value < model::controlMinimum(control) || point.value > model::controlMaximum(control)) {
        throw AppError(model::controlId(control) + " values go from " + std::to_string(model::controlMinimum(control)) +
                       " to " + std::to_string(model::controlMaximum(control)));
    }
    return point;
}

Json setControl(ProjectController& controller, const Nav& nav, const Json& params) {
    const model::ClipId clip = targetClip(controller, nav, params);
    const model::MidiClip& midi = midiClipOf(controller, clip);
    const model::Control control = checkedControl(param<std::string>(params, "lane"));
    const auto points = optionalParam<std::vector<Json>>(params, "points").value_or(std::vector<Json>{}) |
                        std::views::transform([&](const Json& entry) { return checkedPoint(entry, control, midi); }) |
                        std::ranges::to<std::vector>();
    const auto from = optionalParam<core::Ticks>(params, "from").value_or(points.empty() ? 0 : points.front().at);
    const auto to = optionalParam<core::Ticks>(params, "to").value_or(points.empty() ? -1 : points.back().at);
    const int channel = model::controlChannel(midi, control);
    editNotes(controller, clip, "edit controller",
              [&](model::MidiClip& edited) { model::replaceControl(edited, control, from, to, points, channel); });
    return {{"clip", clip.value},
            {"lane", model::controlId(control)},
            {"pointCount", model::controlPoints(midiClipOf(controller, clip), control).size()}};
}

}  // namespace

void registerControlActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addSetting(
        {.id = kLanesSetting,
         .title = "Piano roll controller lanes",
         .category = "Settings / Editing",
         .type = SettingType::List,
         .defaultValue = Json::array({kVelocityLane}),
         .hint = "Lanes under the notes, from top to bottom: velocity, pitchBend, pressure or cc<number>"});
    const Json laneSchema = {{"type", "string"}};
    registry.addAction({.id = "midi.addLane",
                        .title = "Add controller lane",
                        .description = "Shows one more lane under the piano roll notes, by default the first standard "
                                       "lane not shown yet.",
                        .aliases = {"show controller", "MIDI draw", "CC lane"},
                        .category = "MIDI",
                        .schema = {{"properties", {{"lane", laneSchema}}}},
                        .run = [&registry](const Json& params) { return addLane(registry, params); }});
    registry.addAction({.id = "midi.removeLane",
                        .title = "Remove controller lane",
                        .category = "MIDI",
                        .schema = {{"required", {"lane"}}, {"properties", {{"lane", laneSchema}}}},
                        .run = [&registry](const Json& params) { return removeLane(registry, params); }});
    registry.addAction(
        {.id = "midi.changeLane",
         .title = "Show controller in lane",
         .description = "Shows the to lane in place of lane under the piano roll notes.",
         .category = "MIDI",
         .schema = {{"required", {"lane", "to"}}, {"properties", {{"lane", laneSchema}, {"to", laneSchema}}}},
         .run = [&registry](const Json& params) { return changeLane(registry, params); }});
    registry.addAction(
        {.id = "midi.setControl",
         .title = "Edit controller values",
         .description = "Replaces the lane's events from from to to, both included, with points, each {at, value} or "
                        "[at, value]. at is ticks from the clip start. Values go from 0 to 127, and from -8192 to 8191 "
                        "for pitchBend. A "
                        "point that repeats the value before it is dropped, and no points deletes the range. "
                        "project.clipEvents lists each lane's points under controls.",
         .aliases = {"draw controller", "draw modulation", "draw pitch bend", "delete controller events"},
         .category = "MIDI",
         .schema = {{"required", {"lane"}},
                    {"properties",
                     {{"clip", {{"type", "string"}}},
                      {"lane", laneSchema},
                      {"from", {{"type", "integer"}}},
                      {"to", {{"type", "integer"}}},
                      {"points",
                       {{"type", "array"},
                        {"items",
                         {{"description", "{at, value} or [at, value]"},
                          {"properties", {{"at", {{"type", "integer"}}}, {"value", {{"type", "integer"}}}}}}}}}}}},
         .run = [&controller, &nav](const Json& params) { return setControl(controller, nav, params); }});
}

}  // namespace emerick::app
