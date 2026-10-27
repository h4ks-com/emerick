#include <algorithm>
#include <ranges>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/model/clip_edit.hpp"

namespace emerick::app {

namespace {

// Turning on a cycle that has no region gives it the four bars from the playhead's bar, as a starting point to drag.
constexpr core::Ticks kDefaultBars = 4;

Json setCycle(ProjectController& controller, const model::Cycle& cycle, std::string name) {
    controller.project().setCycle(cycle, std::move(name));
    return Json{{"cycle", cycleState(controller.project().document())}};
}

Json toggleCycle(ProjectController& controller, const Json& params) {
    model::Cycle cycle = controller.project().document().cycle;
    cycle.enabled = optionalParam<bool>(params, "enabled").value_or(!cycle.enabled);
    if (cycle.enabled && cycle.end <= cycle.start) {
        const core::Ticks bar = controller.ticksPerBar();
        cycle.start = controller.positionTicks() / bar * bar;
        cycle.end = cycle.start + (kDefaultBars * bar);
    }
    return setCycle(controller, cycle, "toggle cycle");
}

Json setLocators(ProjectController& controller, const Json& params) {
    const core::TempoMap& tempo = controller.project().document().tempo;
    model::Cycle cycle = controller.project().document().cycle;
    cycle.start = optionalPosition(params, "start", tempo).value_or(cycle.start);
    cycle.end = optionalPosition(params, "end", tempo).value_or(cycle.end);
    cycle.enabled = optionalParam<bool>(params, "enabled").value_or(cycle.enabled);
    if (cycle.start < 0 || cycle.end <= cycle.start) {
        throw AppError("the cycle needs 0 <= start < end, got start " + std::to_string(cycle.start) + " and end " +
                       std::to_string(cycle.end));
    }
    return setCycle(controller, cycle, "set cycle");
}

Json cycleClips(ProjectController& controller, const Nav& nav, const Json& params) {
    const auto clips =
        targetClips(controller, nav, params) |
        std::views::transform([&](const model::ClipId& id) { return controller.project().findClip(id)->clip; }) |
        std::ranges::to<std::vector>();
    return setCycle(controller,
                    {.enabled = true,
                     .start = std::ranges::min(clips | std::views::transform([](const model::Clip* clip) {
                                                   return model::clipStart(*clip);
                                               })),
                     .end = std::ranges::max(
                         clips | std::views::transform([](const model::Clip* clip) { return model::clipEnd(*clip); }))},
                    "set cycle to clips");
}

}  // namespace

void registerCycleActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addAction({.id = "cycle.toggle",
                        .title = "Toggle cycle",
                        .description = "Repeats playback between the cycle locators; recording plays straight on",
                        .aliases = {"loop", "cycle mode", "repeat region"},
                        .category = "Transport",
                        .shortcut = "C",
                        .schema = {{"properties", {{"enabled", {{"type", "boolean"}}}}}},
                        .run = [&controller](const Json& params) { return toggleCycle(controller, params); },
                        .menus = {kTransportMenu}});
    registry.addAction({.id = "cycle.set",
                        .title = "Set cycle locators",
                        .aliases = {"set loop", "loop region", "locators"},
                        .category = "Transport",
                        .schema = {{"properties",
                                    {{"start", {{"type", "integer"}}},
                                     {"end", {{"type", "integer"}}},
                                     {"enabled", {{"type", "boolean"}}}}}},
                        .run = [&controller](const Json& params) { return setLocators(controller, params); }});
    registry.addAction({.id = "cycle.setFromClips",
                        .title = "Set cycle to selected clips",
                        .aliases = {"loop selection", "cycle selection", "loop clip region"},
                        .category = "Transport",
                        .shortcut = "Ctrl+U",
                        .schema = {{"properties", {{"clip", {{"type", "string"}}}}}},
                        .run = [&controller, &nav](const Json& params) { return cycleClips(controller, nav, params); },
                        .menus = {kTransportMenu, kClipContextMenu}});
}

}  // namespace emerick::app
