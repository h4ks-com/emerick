#include <algorithm>
#include <array>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/automation_params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/read_only_titles.hpp"

namespace emerick::app {

namespace {

const TrackType* typeOf(const Registry& registry, const ProjectController& controller, const model::TrackId& id) {
    return registry.trackType(controller.project().track(id)->kind);
}

model::TrackId busParam(const Registry& registry, const ProjectController& controller, const Json& params) {
    const model::TrackId bus = trackParam(controller.project(), params, "bus");
    const TrackType* type = typeOf(registry, controller, bus);
    if (type == nullptr || !type->routes.bus) {
        throw AppError("track " + controller.project().track(bus)->name + " is not a bus, run track.addBus first");
    }
    return bus;
}

// Only tracks that play audio can send or play into a bus.
model::TrackId sourceParam(const Registry& registry, const ProjectController& controller, const Nav& nav,
                           const Json& params) {
    const model::TrackId track = targetTrack(controller, nav, params);
    const TrackType* type = typeOf(registry, controller, track);
    const model::Track& source = *controller.project().track(track);
    if (type == nullptr || !playsAudio(*type, source)) {
        throw AppError(playsNoAudio(type, source, "send from an audio track or a bus"));
    }
    return track;
}

void checkNoLoop(const ProjectController& controller, const model::TrackId& track, const model::TrackId& bus) {
    const model::Document& document = controller.project().document();
    if (track == bus || model::reaches(document, bus, track)) {
        throw AppError("bus " + findTrack(document, bus)->name + " already feeds " + findTrack(document, track)->name +
                       ", so routing it there would loop");
    }
}

model::Send& sendTo(model::Track& track, const model::TrackId& bus) {
    const auto found = std::ranges::find(track.sends, bus, &model::Send::bus);
    if (found == track.sends.end()) {
        throw AppError("track " + track.name + " has no send to that bus, run send.add first");
    }
    return *found;
}

Json addSend(ProjectController& controller, const model::TrackId& track, const model::TrackId& bus,
             const Json& params) {
    checkNoLoop(controller, track, bus);
    const model::Track& source = *controller.project().track(track);
    if (std::ranges::contains(source.sends, bus, &model::Send::bus)) {
        throw AppError("track " + source.name + " already sends to " + controller.project().track(bus)->name);
    }
    const model::Send send{.bus = bus,
                           .levelDb = optionalParam<double>(params, "db").value_or(0.0),
                           .pre = optionalParam<bool>(params, "pre").value_or(false),
                           .enabled = true};
    return editTrack(controller, track, "add send to " + controller.project().track(bus)->name,
                     [&](model::Track& edited) { edited.sends.push_back(send); });
}

std::vector<MenuChoice> busChoices(const Registry& registry, const ProjectController& controller) {
    return controller.project().document().tracks | std::views::filter([&](const model::Track& track) {
               const TrackType* type = registry.trackType(track.kind);
               return type != nullptr && type->routes.bus;
           }) |
           std::views::transform([](const model::Track& track) {
               return MenuChoice{.id = track.id.value, .title = track.name, .params = {{"bus", track.id.value}}};
           }) |
           std::ranges::to<std::vector>();
}

struct SendFlag {
    const char* id;
    const char* title;
    const char* alias;
    bool model::Send::* field;
};

constexpr std::array kSendFlags{
    SendFlag{kSendPreKeys.id, kSendPreKeys.title, "pre fader send", &model::Send::pre},
    SendFlag{"send.setEnabled", "Turn send on or off", "bypass send", &model::Send::enabled}};

void registerSendEdits(Registry& registry, ProjectController& controller, const Nav& nav) {
    registry.addAction({.id = "send.remove",
                        .title = "Remove send",
                        .aliases = {"delete send"},
                        .category = "Tracks / Sends",
                        .schema = {{"required", {"bus"}},
                                   {"properties", {{"track", {{"type", "string"}}}, {"bus", {{"type", "string"}}}}}},
                        .run = [&registry, &controller, &nav](const Json& params) {
                            const auto track = sourceParam(registry, controller, nav, params);
                            const auto bus = trackParam(controller.project(), params, "bus");
                            return editTrack(controller, track, "remove send", [&](model::Track& edited) {
                                sendTo(edited, bus);
                                std::erase_if(edited.sends, [&](const model::Send& send) { return send.bus == bus; });
                            });
                        }});
    registry.addAction(
        {.id = kSendLevelKeys.id,
         .title = kSendLevelKeys.title,
         .aliases = {"send amount", "aux level"},
         .category = "Tracks / Sends",
         .schema = {{"required", {"bus", "db"}},
                    {"properties",
                     {{"track", {{"type", "string"}}}, {"bus", {{"type", "string"}}}, {"db", {{"type", "number"}}}}}},
         .run = [&registry, &controller, &nav](const Json& params) {
             const auto track = sourceParam(registry, controller, nav, params);
             const auto bus = trackParam(controller.project(), params, "bus");
             const auto db = param<double>(params, "db");
             if (controller.writeAutomation(track, sendParamId(bus), db)) {
                 return trackResult(controller, track);
             }
             return editTrack(controller, track, "set send level",
                              [&](model::Track& edited) { sendTo(edited, bus).levelDb = db; });
         }});
    for (const SendFlag& flag : kSendFlags) {
        registry.addAction({.id = flag.id,
                            .title = flag.title,
                            .aliases = {flag.alias},
                            .category = "Tracks / Sends",
                            .schema = {{"required", {"bus"}},
                                       {"properties",
                                        {{"track", {{"type", "string"}}},
                                         {"bus", {{"type", "string"}}},
                                         {"value", {{"type", "boolean"}}}}}},
                            .run = [&registry, &controller, &nav, flag](const Json& params) {
                                const auto track = sourceParam(registry, controller, nav, params);
                                const auto bus = trackParam(controller.project(), params, "bus");
                                return editTrack(controller, track, flag.title, [&](model::Track& edited) {
                                    model::Send& send = sendTo(edited, bus);
                                    send.*flag.field =
                                        optionalParam<bool>(params, "value").value_or(!(send.*flag.field));
                                });
                            }});
    }
}

}  // namespace

void removeTrack(ProjectController& controller, const model::TrackId& id) {
    model::Project& project = controller.project();
    const std::string name = "remove track " + project.track(id)->name;
    const auto feeding =
        project.document().tracks | std::views::filter([&](const model::Track& track) {
            return std::ranges::contains(model::busesFedBy(track), id) || track.midiIn.sourceTrack == id;
        }) |
        std::views::transform(&model::Track::id) | std::ranges::to<std::vector>();
    project.group(name, [&] {
        for (const model::TrackId& track : feeding) {
            project.editTrack(track, name, [&](model::Track& edited) {
                std::erase_if(edited.sends, [&](const model::Send& send) { return send.bus == id; });
                std::erase_if(edited.automation,
                              [&](const model::AutomationLane& lane) { return lane.param == sendParamId(id); });
                if (edited.outputBus == id) {
                    edited.outputBus = {};
                }
                if (edited.midiIn.sourceTrack == id) {
                    edited.midiIn.sourceTrack = {};
                }
            });
        }
        project.removeTrack(id);
    });
}

void registerSendActions(Registry& registry, ProjectController& controller, Nav& nav) {
    const auto onAudio = [&registry, &controller, &nav](const Json& params) {
        sourceParam(registry, controller, nav, params);
        return true;
    };
    registry.addAction({.id = "send.add",
                        .title = "Add send",
                        .aliases = {"send to bus", "aux send"},
                        .category = "Tracks / Sends",
                        .appliesTo = onAudio,
                        .schema = {{"required", {"bus"}},
                                   {"properties",
                                    {{"track", {{"type", "string"}}},
                                     {"bus", {{"type", "string"}}},
                                     {"db", {{"type", "number"}}},
                                     {"pre", {{"type", "boolean"}}}}}},
                        .run =
                            [&registry, &controller, &nav](const Json& params) {
                                return addSend(controller, sourceParam(registry, controller, nav, params),
                                               busParam(registry, controller, params), params);
                            },
                        .menus = {kTrackContextMenu},
                        .submenu = [&registry, &controller] { return busChoices(registry, controller); }});
    registerSendEdits(registry, controller, nav);
    registry.addAction({.id = "track.setOutputBus",
                        .title = "Set track output bus",
                        .aliases = {"route to bus", "output to bus"},
                        .category = "Tracks / Routing",
                        .appliesTo = onAudio,
                        .schema = {{"required", {"bus"}},
                                   {"properties", {{"track", {{"type", "string"}}}, {"bus", {{"type", "string"}}}}}},
                        .run =
                            [&registry, &controller, &nav](const Json& params) {
                                const auto track = sourceParam(registry, controller, nav, params);
                                model::TrackId bus;
                                if (!param<std::string>(params, "bus").empty()) {
                                    bus = busParam(registry, controller, params);
                                    checkNoLoop(controller, track, bus);
                                }
                                return editTrack(controller, track, "set track output bus",
                                                 [&](model::Track& edited) { edited.outputBus = bus; });
                            },
                        .menus = {kTrackContextMenu},
                        .submenu =
                            [&registry, &controller] {
                                std::vector<MenuChoice> choices = busChoices(registry, controller);
                                choices.insert(choices.begin(),
                                               {.id = "", .title = "Audio output", .params = {{"bus", ""}}});
                                return choices;
                            }});
    registry.addAction(
        {.id = "send.toNewBus",
         .title = "Create bus and send selected tracks to it",
         .aliases = {"new aux from selection", "send to new bus"},
         .category = "Tracks / Sends",
         .appliesTo = onAudio,
         .schema =
             {{"properties",
               {{"track", {{"type", "string"}}}, {"tracks", {{"type", "array"}}}, {"name", {{"type", "string"}}}}}},
         .run =
             [&registry, &controller, &nav](const Json& params) {
                 const auto names = optionalParam<std::vector<std::string>>(params, "tracks");
                 const std::vector<model::TrackId> tracks =
                     names ? *names | std::views::transform([&](const std::string& name) {
                         return sourceParam(registry, controller, nav, {{"track", name}});
                     }) | std::ranges::to<std::vector>()
                           : std::vector{sourceParam(registry, controller, nav, params)};
                 Json result;
                 controller.project().group("create bus and send", [&] {
                     Json busParams = Json::object();
                     if (const auto name = optionalParam<std::string>(params, "name")) {
                         busParams["name"] = *name;
                     }
                     const model::TrackId bus{registry.run("track.addBus", busParams).at("id").get<std::string>()};
                     for (const model::TrackId& track : tracks) {
                         addSend(controller, track, bus, Json::object());
                     }
                     nav.setSelectedTrack(qs(bus.value));
                     result = trackResult(controller, bus);
                 });
                 return result;
             },
         .menus = {kTrackMenu, kTrackContextMenu}});
}

}  // namespace emerick::app
