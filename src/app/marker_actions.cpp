#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"

namespace emerick::app {

namespace {

constexpr int kNumberedMarkers = 9;

const std::vector<model::Marker>& markersOf(const ProjectController& controller) {
    return controller.project().document().markers;
}

Json markerResult(const ProjectController& controller, const model::MarkerId& id) {
    const auto& markers = markersOf(controller);
    const auto found = std::ranges::find(markers, id, &model::Marker::id);
    if (found == markers.end()) {
        return Json::object();
    }
    return markerState(*found, static_cast<int>(found - markers.begin()) + 1, controller.project().document().tempo);
}

// The marker named by params, or the selected marker when params names none.
model::MarkerId targetMarker(const ProjectController& controller, const Nav& nav, const Json& params) {
    if (const auto given = optionalParam<std::string>(params, "marker")) {
        const model::MarkerId id{*given};
        if (model::findMarker(controller.project().document(), id) == nullptr) {
            throw AppError("no marker " + *given + ", read the project state for marker ids");
        }
        return id;
    }
    if (const auto selected = selectedMarker(controller, nav)) {
        return *selected;
    }
    throw AppError("select a marker first, or pass marker");
}

std::optional<int> colorParam(const Json& params) {
    const auto color = optionalParam<int>(params, "color");
    if (color && (*color < 0 || *color >= kTrackColors)) {
        throw AppError("color must be 0 to 7, or null for no color");
    }
    return color;
}

Json editMarker(ProjectController& controller, const model::MarkerId& id, std::string name,
                const std::function<void(model::Marker&)>& edit) {
    auto markers = markersOf(controller);
    edit(*std::ranges::find(markers, id, &model::Marker::id));
    controller.project().setMarkers(std::move(markers), std::move(name));
    return markerResult(controller, id);
}

Json locateTo(ProjectController& controller, const model::Marker& marker) {
    controller.locate(marker.at);
    return Json{{"marker", markerResult(controller, marker.id)}, {"transport", transportState(controller)}};
}

Json addMarker(ProjectController& controller, Nav& nav, const Json& params) {
    const auto at =
        optionalPosition(params, "at", controller.project().document().tempo).value_or(controller.positionTicks());
    if (at < 0) {
        throw AppError("at must be at or after bar 1");
    }
    model::Marker marker{.name = optionalParam<std::string>(params, "name")
                                     .value_or("Marker " + std::to_string(markersOf(controller).size() + 1)),
                         .at = at,
                         .color = colorParam(params)};
    const model::MarkerId id = marker.id;
    auto markers = markersOf(controller);
    markers.push_back(std::move(marker));
    controller.project().setMarkers(std::move(markers), "add marker");
    nav.setSelectedMarker(qs(id.value));
    return markerResult(controller, id);
}

// Steps to the first marker after the playhead, or with step -1 to the last one before it.
Json stepToMarker(ProjectController& controller, int step) {
    const auto& markers = markersOf(controller);
    const core::Ticks now = controller.positionTicks();
    const auto next = std::ranges::find_if(markers, [&](const model::Marker& marker) { return marker.at > now; });
    const auto previous = std::ranges::find_if(markers | std::views::reverse,
                                               [&](const model::Marker& marker) { return marker.at < now; });
    if (step > 0 && next != markers.end()) {
        return locateTo(controller, *next);
    }
    if (step < 0 && previous != std::ranges::rend(markers)) {
        return locateTo(controller, *previous);
    }
    throw AppError(step > 0 ? "no marker after the playhead" : "no marker before the playhead");
}

Json gotoNumber(ProjectController& controller, int number) {
    const auto& markers = markersOf(controller);
    if (number < 1 || number > std::ssize(markers)) {
        throw AppError("no marker " + std::to_string(number) + ", the project has " + std::to_string(markers.size()));
    }
    return locateTo(controller, markers[static_cast<std::size_t>(number - 1)]);
}

struct MarkerStep {
    const char* id;
    const char* title;
    const char* alias;
    const char* shortcut;
    int step;
};

constexpr std::array kMarkerSteps{
    MarkerStep{"marker.next", "Go to next marker", "next section", "Alt+.", 1},
    MarkerStep{"marker.previous", "Go to previous marker", "previous section", "Alt+,", -1},
};

void registerMarkerNavigation(Registry& registry, ProjectController& controller, Nav& nav) {
    for (const MarkerStep& step : kMarkerSteps) {
        registry.addAction({.id = step.id,
                            .title = step.title,
                            .aliases = {step.alias},
                            .category = "Markers",
                            .shortcut = step.shortcut,
                            .run = [&controller, step](const Json&) { return stepToMarker(controller, step.step); },
                            .menus = {kTransportMenu}});
    }
    registry.addAction(
        {.id = "marker.goto",
         .title = "Go to marker",
         .aliases = {"locate marker", "jump to marker"},
         .category = "Markers",
         .schema = {{"properties", {{"marker", {{"type", "string"}}}, {"number", {{"type", "integer"}}}}}},
         .run = [&controller, &nav](const Json& params) {
             if (const auto number = optionalParam<int>(params, "number")) {
                 return gotoNumber(controller, *number);
             }
             const model::MarkerId id = targetMarker(controller, nav, params);
             nav.setSelectedMarker(qs(id.value));
             return locateTo(controller, *model::findMarker(controller.project().document(), id));
         }});
    for (const int number : std::views::iota(1, kNumberedMarkers + 1)) {
        const std::string text = std::to_string(number);
        registry.addAction({.id = "marker.goto" + text,
                            .title = "Go to marker " + text,
                            .category = "Markers",
                            .shortcut = text,
                            .run = [&controller, number](const Json&) { return gotoNumber(controller, number); }});
    }
}

void registerMarkerEdits(Registry& registry, ProjectController& controller, Nav& nav) {
    const Json markerOnly = {{"properties", {{"marker", {{"type", "string"}}}}}};
    registry.addAction(
        {.id = "marker.add",
         .title = "Add marker",
         .aliases = {"create marker", "new marker", "add section marker"},
         .category = "Markers",
         .shortcut = "Alt+'",
         .schema = {{"properties",
                     {{"at", Json::object()}, {"name", {{"type", "string"}}}, {"color", {{"type", "integer"}}}}}},
         .run = [&controller, &nav](const Json& params) { return addMarker(controller, nav, params); },
         .menus = {kTransportMenu, kMarkerLaneContextMenu}});
    registry.addAction({.id = "marker.beginRename",
                        .title = "Rename marker",
                        .aliases = {"edit marker name"},
                        .category = "Markers",
                        .schema = markerOnly,
                        .run =
                            [&controller, &nav](const Json& params) {
                                const model::MarkerId id = targetMarker(controller, nav, params);
                                nav.setSelectedMarker(qs(id.value));
                                nav.setRenamingMarker(qs(id.value));
                                return markerResult(controller, id);
                            },
                        .menus = {kMarkerContextMenu}});
    registry.addAction({.id = "marker.rename",
                        .title = "Set marker name",
                        .category = "Markers",
                        .schema = {{"required", {"name"}}, {"properties", {{"marker", {{"type", "string"}}}}}},
                        .run = [&controller, &nav](const Json& params) {
                            const model::MarkerId id = targetMarker(controller, nav, params);
                            const auto name = param<std::string>(params, "name");
                            nav.setRenamingMarker({});
                            return editMarker(controller, id, "rename marker to " + name,
                                              [&](model::Marker& marker) { marker.name = name; });
                        }});
    registry.addAction({.id = "marker.move",
                        .title = "Move marker",
                        .aliases = {"drag marker"},
                        .category = "Markers",
                        .schema = {{"required", {"at"}}, {"properties", {{"marker", {{"type", "string"}}}}}},
                        .run = [&controller, &nav](const Json& params) {
                            const model::MarkerId id = targetMarker(controller, nav, params);
                            const core::Ticks at = positionParam(params, "at", controller.project().document().tempo);
                            if (at < 0) {
                                throw AppError("at must be at or after bar 1");
                            }
                            return editMarker(controller, id, "move marker",
                                              [&](model::Marker& marker) { marker.at = at; });
                        }});
    registry.addAction(
        {.id = "marker.setColor",
         .title = "Set marker color",
         .category = "Markers",
         .schema = {{"properties", {{"marker", {{"type", "string"}}}, {"color", {{"type", "integer"}}}}}},
         .run =
             [&controller, &nav](const Json& params) {
                 const model::MarkerId id = targetMarker(controller, nav, params);
                 const auto color = colorParam(params);
                 return editMarker(controller, id, "set marker color",
                                   [&](model::Marker& marker) { marker.color = color; });
             },
         .menus = {kMarkerContextMenu},
         .submenu = colorChoices,
         .current =
             [&controller, &nav](const Json& params) {
                 const auto color =
                     model::findMarker(controller.project().document(), targetMarker(controller, nav, params))->color;
                 return color ? std::to_string(*color) : std::string{};
             }});
    registry.addAction({.id = "marker.delete",
                        .title = "Delete marker",
                        .aliases = {"remove marker"},
                        .category = "Markers",
                        .schema = markerOnly,
                        .run =
                            [&controller, &nav](const Json& params) {
                                deleteMarker(controller, targetMarker(controller, nav, params));
                                return Json{{"markers", markersState(controller.project().document())}};
                            },
                        .menus = {kMarkerContextMenu}});
    registry.addAction({.id = "marker.select",
                        .title = "Select marker",
                        .category = "Markers",
                        .schema = {{"required", {"marker"}}},
                        .run = [&controller, &nav](const Json& params) {
                            const model::MarkerId id = targetMarker(controller, nav, params);
                            nav.setSelectedMarker(qs(id.value));
                            return markerResult(controller, id);
                        }});
}

}  // namespace

std::optional<model::MarkerId> selectedMarker(const ProjectController& controller, const Nav& nav) {
    model::MarkerId id{nav.selectedMarker().toStdString()};
    if (id.empty() || model::findMarker(controller.project().document(), id) == nullptr) {
        return std::nullopt;
    }
    return id;
}

void deleteMarker(ProjectController& controller, const model::MarkerId& id) {
    auto markers = markersOf(controller);
    std::erase_if(markers, [&](const model::Marker& marker) { return marker.id == id; });
    controller.project().setMarkers(std::move(markers), "delete marker");
}

void registerMarkerActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registerMarkerEdits(registry, controller, nav);
    registerMarkerNavigation(registry, controller, nav);
}

}  // namespace emerick::app
