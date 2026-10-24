#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/audio_file.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/model/midi_notes.hpp"

namespace emerick::app {

namespace {

enum class Direction : std::uint8_t { input, output };

std::vector<int> defaultChannels(int count) { return count >= 2 ? std::vector{1, 2} : std::vector{1}; }

// A field missing from params keeps its value in route, except that a new device without channels gets 1-2, or 1.
model::AudioRoute mergeAudioRoute(ProjectController& controller, model::AudioRoute route, const Json& params,
                                  Direction direction) {
    const auto deviceId = optionalParam<std::string>(params, "device");
    const auto channels = optionalParam<std::vector<int>>(params, "channels");
    const bool newDevice = deviceId && *deviceId != route.deviceId && !channels;
    route.deviceId = deviceId.value_or(route.deviceId);
    route.channels = channels.value_or(route.channels);
    if (route.deviceId.empty()) {
        return {};
    }
    const auto& devices = controller.engine().audioDevices();
    const auto device = std::ranges::find(devices, route.deviceId, &engine::AudioDeviceInfo::id);
    if (device == devices.end()) {
        throw AppError("no audio device " + route.deviceId + ", run devices.list for device ids");
    }
    if (const auto problem = engine::unsupportedRate(*device, controller.sampleRate())) {
        throw AppError(*problem);
    }
    const int count = direction == Direction::input ? device->inputChannels : device->outputChannels;
    if (newDevice && count > 0) {
        route.channels = defaultChannels(count);
    }
    if (route.channels.empty() || std::ranges::any_of(route.channels, [&](int c) { return c < 1 || c > count; })) {
        throw AppError("channels for " + device->name + " must be 1 to " + std::to_string(count));
    }
    return route;
}

// A field missing from params keeps its value in route.
model::MidiRoute mergeMidiRoute(ProjectController& controller, model::MidiRoute route, const Json& params,
                                Direction direction) {
    route.portId = optionalParam<std::string>(params, "port").value_or(route.portId);
    route.channel = optionalParam<int>(params, "channel").value_or(route.channel);
    route.thru = optionalParam<bool>(params, "thru").value_or(route.thru);
    route.sysex = optionalParam<bool>(params, "sysex").value_or(route.sysex);
    if (route.channel < 0 || route.channel > kMidiChannels) {
        throw AppError("channel must be 0 (all) to 16");
    }
    if (route.portId.empty()) {
        return route;
    }
    const auto& ports = controller.engine().midiPorts();
    const bool exists = std::ranges::any_of(ports, [&](const engine::MidiPortInfo& port) {
        return port.id == route.portId && port.isInput == (direction == Direction::input);
    });
    if (!exists) {
        throw AppError("no MIDI " + std::string{direction == Direction::input ? "input" : "output"} + " port " +
                       route.portId + ", run devices.list for port ids");
    }
    return route;
}

model::AudioRoute defaultAudioRoute(ProjectController& controller, Direction direction) {
    const auto& devices = controller.engine().audioDevices();
    const auto channels = [direction](const engine::AudioDeviceInfo& device) {
        return direction == Direction::input ? device.inputChannels : device.outputChannels;
    };
    const auto device = std::ranges::find_if(devices, [&](const auto& d) {
        return channels(d) > 0 && !engine::unsupportedRate(d, controller.sampleRate());
    });
    if (device == devices.end()) {
        return {};
    }
    return {device->id, defaultChannels(channels(*device))};
}

Json routeJson(const model::AudioRoute& route) { return {{"device", route.deviceId}, {"channels", route.channels}}; }

model::AudioRoute routeFromJson(const Json& value) {
    return {value.at("device").get<std::string>(), value.at("channels").get<std::vector<int>>()};
}

struct AudioDefault {
    Direction direction;
    const char* setting;
    const char* settingTitle;
    const char* actionId;
    const char* actionTitle;
    model::AudioRoute model::Track::* field;
};

constexpr std::array kAudioDefaults{
    AudioDefault{Direction::input, "audio.defaultInput", "Default input", "track.setAllAudioIn",
                 "Set input for all audio tracks", &model::Track::audioIn},
    AudioDefault{Direction::output, "audio.defaultOutput", "Default output", "track.setAllAudioOut",
                 "Set output for all audio tracks", &model::Track::audioOut},
};

// A default whose device is gone gives way to the first device that fits, so adding a track still works.
model::AudioRoute newTrackRoute(const Registry& registry, ProjectController& controller, const AudioDefault& entry) {
    try {
        return mergeAudioRoute(controller, {}, registry.settingValue(entry.setting), entry.direction);
    } catch (const AppError&) {
        return defaultAudioRoute(controller, entry.direction);
    }
}

void registerAudioDefault(Registry& registry, ProjectController& controller, const AudioDefault& entry) {
    const Direction direction = entry.direction;
    const char* devicesKey = direction == Direction::input ? "audioIn" : "audioOut";
    registry.addSetting(
        {.id = entry.setting,
         .title = entry.settingTitle,
         .aliases = {direction == Direction::input ? "default audio input" : "default audio output"},
         .category = "Settings / Audio",
         .type = SettingType::AudioRoute,
         .hint =
             direction == Direction::input ? "Input new audio tracks record from" : "Output new audio tracks play to",
         .suggestions = [&controller, devicesKey] { return routeDevices(devicesState(controller)).at(devicesKey); },
         .location = "settings.audio",
         .fallback = [&controller, direction] { return routeJson(defaultAudioRoute(controller, direction)); },
         .merge =
             [&controller, direction](const Json& current, const Json& change) {
                 return routeJson(mergeAudioRoute(controller, routeFromJson(current), change, direction));
             },
         .action = entry.actionId});
    registry.addAction(
        {.id = entry.actionId,
         .title = entry.actionTitle,
         .aliases = {direction == Direction::input ? "apply input to all audio tracks"
                                                   : "apply output to all audio tracks"},
         .category = "Tracks / Routing",
         .schema = {{"properties", {{"device", {{"type", "string"}}}, {"channels", {{"type", "array"}}}}}},
         .run =
             [&registry, &controller, entry](const Json& params) {
                 const auto route = mergeAudioRoute(controller, routeFromJson(registry.settingValue(entry.setting)),
                                                    params, entry.direction);
                 const auto ids = controller.project().document().tracks |
                                  std::views::filter([&](const model::Track& track) {
                                      const TrackType* type = registry.trackType(track.kind);
                                      return type != nullptr && type->routes.audio;
                                  }) |
                                  std::views::transform(&model::Track::id) | std::ranges::to<std::vector>();
                 controller.project().group(entry.actionTitle, [&] {
                     for (const model::TrackId& id : ids) {
                         controller.project().editTrack(id, entry.actionTitle,
                                                        [&](model::Track& track) { track.*entry.field = route; });
                     }
                 });
                 return Json{
                     {"route", routeJson(route)},
                     {"tracks", ids | std::views::transform(&model::TrackId::value) | std::ranges::to<std::vector>()}};
             },
         .menus = {kTrackContextMenu, kLaneContextMenu}});
}

model::MidiRoute defaultMidiInput(ProjectController& controller) {
    const auto& ports = controller.engine().midiPorts();
    const auto port = std::ranges::find_if(ports, &engine::MidiPortInfo::isInput);
    return port == ports.end() ? model::MidiRoute{} : model::MidiRoute{port->id, 0, false, false};
}

// A track listening to one channel of a profiled device takes that channel's part name and icon.
void applyDeviceProfile(const Registry& registry, model::Track& track) {
    const MidiDeviceProfile* profile = registry.midiDeviceProfile(track.midiIn.portId);
    if (profile == nullptr) {
        return;
    }
    if (const auto name = profile->channelNames.find(track.midiIn.channel); name != profile->channelNames.end()) {
        track.name = name->second;
    }
    if (const auto icon = profile->channelIcons.find(track.midiIn.channel); icon != profile->channelIcons.end()) {
        track.icon = icon->second;
    }
}

Json routeParams(const Json& params, const char* key) {
    return optionalParam<Json>(params, key).value_or(Json::object());
}

// The new track takes its name from params, or the kind's title and its position.
Json addTrack(ProjectController& controller, Nav& nav, const TrackType& type, model::Track track, const Json& params) {
    const std::size_t count = controller.project().document().tracks.size();
    track.kind = type.kind;
    if (track.icon.empty()) {
        track.icon = type.icon;
    }
    if (track.name.empty()) {
        track.name = type.title + " " + std::to_string(count + 1);
    }
    track.name = optionalParam<std::string>(params, "name").value_or(track.name);
    track.color = static_cast<int>(count % kTrackColors);
    const model::TrackId id = track.id;
    controller.project().addTrack(std::move(track));
    nav.setSelectedTrack(qs(id.value));
    return trackResult(controller, id);
}

// The caller sets run, which looks its track type up when it runs, because the registry moves types as it grows.
Action addTrackAction(const std::string& actionId, const std::string& label, const std::string& routeProperty,
                      const std::string& shortcut) {
    return {.id = actionId,
            .title = "Add " + label + " track",
            .aliases = {"new " + label + " track", "create " + label + " track"},
            .category = "Tracks",
            .shortcut = shortcut,
            .schema = {{"type", "object"},
                       {"properties",
                        {{"name", {{"type", "string"}}},
                         {routeProperty + "In", {{"type", "object"}}},
                         {routeProperty + "Out", {{"type", "object"}}}}}},
            .menus = {kTrackMenu, kLaneContextMenu}};
}

void registerAudioKind(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addTrackType(
        {.kind = "audio",
         .title = "Audio",
         .icon = "waveform",
         .routes = {.audio = true},
         .toEngine = [](const model::Track& track,
                        const PlaybackContext& context) { return audioEngineTrack(track, context.folder); },
         .fromTake =
             [](model::Track& track, const engine::RecordedTake& take, const TakeContext& context) {
                 track.clips.emplace_back(
                     clipFromFile(take.audioFile, context.folder, take.startTick, context.tempo, context.sampleRate));
             }});
    Action add = addTrackAction("track.addAudio", "audio", "audio", "Ctrl+Alt+N");
    add.run = [&registry, &controller, &nav](const Json& params) {
        model::Track track;
        for (const AudioDefault& entry : kAudioDefaults) {
            track.*entry.field = mergeAudioRoute(
                controller, newTrackRoute(registry, controller, entry),
                routeParams(params, entry.direction == Direction::input ? "audioIn" : "audioOut"), entry.direction);
        }
        return addTrack(controller, nav, *registry.trackType("audio"), std::move(track), params);
    };
    registry.addAction(std::move(add));
}

void registerBusKind(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addTrackType(
        {.kind = "bus",
         .title = "Bus",
         .icon = "arrows-merge",
         .routes = {.bus = true},
         .toEngine = [](const model::Track& track, const PlaybackContext&) { return busEngineTrack(track); }});
    registry.addAction(
        {.id = "track.addBus",
         .title = "Add bus",
         .aliases = {"new bus", "add aux", "create aux track"},
         .category = "Tracks",
         .schema = {{"type", "object"},
                    {"properties", {{"name", {{"type", "string"}}}, {"audioOut", {{"type", "object"}}}}}},
         .run =
             [&registry, &controller, &nav](const Json& params) {
                 model::Track track;
                 const auto* output = std::ranges::find(kAudioDefaults, Direction::output, &AudioDefault::direction);
                 track.audioOut = mergeAudioRoute(controller, newTrackRoute(registry, controller, *output),
                                                  routeParams(params, "audioOut"), Direction::output);
                 return addTrack(controller, nav, *registry.trackType("bus"), std::move(track), params);
             },
         .menus = {kTrackMenu, kLaneContextMenu}});
}

model::MidiClip midiClipFromTake(const engine::RecordedTake& take, core::Ticks stopTicks) {
    model::MidiClip clip;
    clip.start = take.startTick;
    clip.events = take.midiEvents | std::views::transform([](const engine::MidiEvent& event) {
                      return model::MidiEvent{.at = event.tick, .bytes = event.bytes};
                  }) |
                  std::ranges::to<std::vector>();
    const core::Ticks lastEvent = clip.events.empty() ? 0 : clip.events.back().at + 1;
    clip.length = std::max(stopTicks - take.startTick, lastEvent);
    model::gatherNotes(clip);
    return clip;
}

void registerMidiKind(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addTrackType(
        {.kind = "midi",
         .title = "MIDI",
         .icon = "piano-keys",
         .routes = {.midi = true},
         .toEngine = [](const model::Track& track, const PlaybackContext&) { return midiEngineTrack(track); },
         .fromTake =
             [](model::Track& track, const engine::RecordedTake& take, const TakeContext& context) {
                 track.clips.emplace_back(midiClipFromTake(take, context.stopTicks));
             }});
    Action add = addTrackAction("track.addMidi", "MIDI", "midi", "Ctrl+Alt+S");
    add.run = [&registry, &controller, &nav](const Json& params) {
        model::Track track;
        track.midiIn =
            mergeMidiRoute(controller, defaultMidiInput(controller), routeParams(params, "midiIn"), Direction::input);
        track.midiOut = mergeMidiRoute(controller, {}, routeParams(params, "midiOut"), Direction::output);
        applyDeviceProfile(registry, track);
        return addTrack(controller, nav, *registry.trackType("midi"), std::move(track), params);
    };
    registry.addAction(std::move(add));
}

void registerRouteActions(Registry& registry, ProjectController& controller, const Nav& nav) {
    const auto addAudioRoute = [&](const char* actionId, const char* title, Direction direction,
                                   model::AudioRoute model::Track::* field) {
        registry.addAction(
            {.id = actionId,
             .title = title,
             .aliases = {direction == Direction::input ? "audio input" : "audio output"},
             .category = "Tracks / Routing",
             .schema = {{"required", {"track"}},
                        {"properties",
                         {{"device", {{"type", "string"}}},
                          {"channels", {{"type", "array"}}},
                          {"selection", {{"type", "boolean"}}}}}},
             .run = [&controller, &nav, title, direction, field](const Json& params) {
                 return editTargets(controller, nav, params, title, [&](const model::TrackId& id) {
                     const auto route =
                         mergeAudioRoute(controller, controller.project().track(id)->*field, params, direction);
                     editTrack(controller, id, title, [&](model::Track& track) { track.*field = route; });
                 });
             }});
    };
    addAudioRoute("track.setAudioIn", "Set track audio input", Direction::input, &model::Track::audioIn);
    addAudioRoute("track.setAudioOut", "Set track audio output", Direction::output, &model::Track::audioOut);
    for (const AudioDefault& entry : kAudioDefaults) {
        registerAudioDefault(registry, controller, entry);
    }

    const auto addMidiRoute = [&](const char* actionId, const char* title, Direction direction,
                                  model::MidiRoute model::Track::* field) {
        registry.addAction({.id = actionId,
                            .title = title,
                            .aliases = {direction == Direction::input ? "midi input" : "midi output"},
                            .category = "Tracks / Routing",
                            .schema = {{"required", {"track"}},
                                       {"properties",
                                        {{"port", {{"type", "string"}}},
                                         {"channel", {{"type", "integer"}}},
                                         {"thru", {{"type", "boolean"}}},
                                         {"sysex", {{"type", "boolean"}}},
                                         {"selection", {{"type", "boolean"}}}}}},
                            .run = [&registry, &controller, &nav, title, direction, field](const Json& params) {
                                return editTargets(controller, nav, params, title, [&](const model::TrackId& id) {
                                    const auto route = mergeMidiRoute(
                                        controller, controller.project().track(id)->*field, params, direction);
                                    editTrack(controller, id, title, [&](model::Track& track) {
                                        track.*field = route;
                                        if (direction == Direction::input) {
                                            applyDeviceProfile(registry, track);
                                        }
                                    });
                                });
                            }});
    };
    addMidiRoute("track.setMidiIn", "Set track MIDI input", Direction::input, &model::Track::midiIn);
    addMidiRoute("track.setMidiOut", "Set track MIDI output", Direction::output, &model::Track::midiOut);
}

}  // namespace

Json trackResult(const ProjectController& controller, const model::TrackId& id) {
    const model::Track* track = controller.project().track(id);
    return track != nullptr
               ? trackState(*track, controller.project().document().tempo, controller.engine().audioDevices())
               : Json::object();
}

Json editTrack(ProjectController& controller, const model::TrackId& id, std::string name,
               const std::function<void(model::Track&)>& edit) {
    controller.project().editTrack(id, std::move(name), edit);
    return trackResult(controller, id);
}

void registerTrackActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registerAudioKind(registry, controller, nav);
    registerMidiKind(registry, controller, nav);
    registerBusKind(registry, controller, nav);
    registerTrackEditActions(registry, controller, nav);
    registerSendActions(registry, controller, nav);
    registerInsertActions(registry, controller, nav);
    registerRouteActions(registry, controller, nav);
}

}  // namespace emerick::app
