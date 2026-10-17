#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "action_params.hpp"
#include "emerick/app/audio_file.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/session_mapping.hpp"

namespace emerick::app {

namespace {

enum class Direction : std::uint8_t { input, output };

Json trackResult(const ProjectController& controller, const model::TrackId& id) {
    const model::Track* track = controller.project().track(id);
    return track != nullptr ? trackState(*track, controller.project().document().tempo) : Json::object();
}

Json editTrack(ProjectController& controller, const model::TrackId& id, std::string name,
               const std::function<void(model::Track&)>& edit) {
    controller.project().editTrack(id, std::move(name), edit);
    return trackResult(controller, id);
}

// A field missing from params keeps its value in route.
model::AudioRoute mergeAudioRoute(ProjectController& controller, model::AudioRoute route, const Json& params,
                                  Direction direction) {
    route.deviceId = optionalParam<std::string>(params, "device").value_or(route.deviceId);
    route.channels = optionalParam<std::vector<int>>(params, "channels").value_or(route.channels);
    if (route.deviceId.empty()) {
        return {};
    }
    const auto devices = controller.engine().audio().devices();
    const auto device = std::ranges::find(devices, route.deviceId, &engine::AudioDeviceInfo::id);
    if (device == devices.end()) {
        throw AppError("no audio device " + route.deviceId + ", run devices.list for device ids");
    }
    const int count = direction == Direction::input ? device->inputChannels : device->outputChannels;
    if (route.channels.empty() || std::ranges::any_of(route.channels, [&](int c) { return c < 1 || c > count; })) {
        throw AppError("channels for " + route.deviceId + " must be 1 to " + std::to_string(count));
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
    const auto ports = controller.engine().midi().ports();
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
    const auto devices = controller.engine().audio().devices();
    const auto channels = [direction](const engine::AudioDeviceInfo& device) {
        return direction == Direction::input ? device.inputChannels : device.outputChannels;
    };
    const auto device = std::ranges::find_if(devices, [&](const auto& d) { return channels(d) > 0; });
    if (device == devices.end()) {
        return {};
    }
    return {device->id, channels(*device) >= 2 ? std::vector{1, 2} : std::vector{1}};
}

model::MidiRoute defaultMidiInput(ProjectController& controller) {
    const auto ports = controller.engine().midi().ports();
    const auto port = std::ranges::find_if(ports, &engine::MidiPortInfo::isInput);
    return port == ports.end() ? model::MidiRoute{} : model::MidiRoute{port->id, 0, false, false};
}

Json routeParams(const Json& params, const char* key) {
    return optionalParam<Json>(params, key).value_or(Json::object());
}

// The new track takes its name from params, or the kind's title and its position.
Json addTrack(ProjectController& controller, Nav& nav, const TrackType& type, model::Track track, const Json& params) {
    const std::size_t count = controller.project().document().tracks.size();
    track.kind = type.kind;
    track.icon = type.icon;
    track.name = optionalParam<std::string>(params, "name").value_or(type.title + " " + std::to_string(count + 1));
    track.color = static_cast<int>(count % kTrackColors);
    const model::TrackId id = track.id;
    controller.project().addTrack(std::move(track));
    nav.setSelectedTrack(qs(id.value));
    return trackResult(controller, id);
}

// The caller sets run, which looks its track type up when it runs, because the registry moves types as it grows.
Action addTrackAction(const std::string& actionId, const std::string& label, const std::string& routeProperty) {
    return {.id = actionId,
            .title = "Add " + label + " track",
            .aliases = {"new " + label + " track", "create " + label + " track"},
            .category = "Tracks",
            .schema = {{"type", "object"},
                       {"properties",
                        {{"name", {{"type", "string"}}},
                         {routeProperty + "In", {{"type", "object"}}},
                         {routeProperty + "Out", {{"type", "object"}}}}}}};
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
    Action add = addTrackAction("track.addAudio", "audio", "audio");
    add.run = [&registry, &controller, &nav](const Json& params) {
        model::Track track;
        track.audioIn = mergeAudioRoute(controller, defaultAudioRoute(controller, Direction::input),
                                        routeParams(params, "audioIn"), Direction::input);
        track.audioOut = mergeAudioRoute(controller, defaultAudioRoute(controller, Direction::output),
                                         routeParams(params, "audioOut"), Direction::output);
        return addTrack(controller, nav, *registry.trackType("audio"), std::move(track), params);
    };
    registry.addAction(std::move(add));
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
    return clip;
}

void registerMidiKind(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addTrackType(
        {.kind = "midi",
         .title = "MIDI",
         .icon = "keyboard",
         .routes = {.midi = true},
         .toEngine = [](const model::Track& track, const PlaybackContext&) { return midiEngineTrack(track); },
         .fromTake =
             [](model::Track& track, const engine::RecordedTake& take, const TakeContext& context) {
                 track.clips.emplace_back(midiClipFromTake(take, context.stopTicks));
             }});
    Action add = addTrackAction("track.addMidi", "MIDI", "midi");
    add.run = [&registry, &controller, &nav](const Json& params) {
        model::Track track;
        track.midiIn =
            mergeMidiRoute(controller, defaultMidiInput(controller), routeParams(params, "midiIn"), Direction::input);
        track.midiOut = mergeMidiRoute(controller, {}, routeParams(params, "midiOut"), Direction::output);
        return addTrack(controller, nav, *registry.trackType("midi"), std::move(track), params);
    };
    registry.addAction(std::move(add));
}

struct TrackFlag {
    std::string_view id;
    std::string_view title;
    std::string_view alias;
    bool model::Track::* field;
    void (ProjectController::*live)(const model::TrackId&, bool);
};

constexpr std::array kTrackFlags{
    TrackFlag{"track.setMute", "Mute track", "silence track", &model::Track::mute, &ProjectController::setLiveMute},
    TrackFlag{"track.setSolo", "Solo track", "listen alone", &model::Track::solo, &ProjectController::setLiveSolo},
    TrackFlag{"track.setArm", "Arm track for recording", "record enable", &model::Track::armed,
              &ProjectController::setLiveArmed},
    TrackFlag{"track.setMonitor", "Monitor track input", "input monitoring", &model::Track::monitor, nullptr}};

void registerRouteActions(Registry& registry, ProjectController& controller) {
    const auto addAudioRoute = [&](const char* actionId, const char* title, Direction direction,
                                   model::AudioRoute model::Track::* field) {
        registry.addAction(
            {.id = actionId,
             .title = title,
             .aliases = {direction == Direction::input ? "audio input" : "audio output"},
             .category = "Tracks / Routing",
             .schema = {{"required", {"track"}},
                        {"properties", {{"device", {{"type", "string"}}}, {"channels", {{"type", "array"}}}}}},
             .run = [&controller, title, direction, field](const Json& params) {
                 const auto id = trackParam(controller.project(), params);
                 const auto route =
                     mergeAudioRoute(controller, controller.project().track(id)->*field, params, direction);
                 return editTrack(controller, id, title, [&](model::Track& track) { track.*field = route; });
             }});
    };
    addAudioRoute("track.setAudioIn", "Set track audio input", Direction::input, &model::Track::audioIn);
    addAudioRoute("track.setAudioOut", "Set track audio output", Direction::output, &model::Track::audioOut);

    const auto addMidiRoute = [&](const char* actionId, const char* title, Direction direction,
                                  model::MidiRoute model::Track::* field) {
        registry.addAction(
            {.id = actionId,
             .title = title,
             .aliases = {direction == Direction::input ? "midi input" : "midi output"},
             .category = "Tracks / Routing",
             .schema = {{"required", {"track"}},
                        {"properties",
                         {{"port", {{"type", "string"}}},
                          {"channel", {{"type", "integer"}}},
                          {"thru", {{"type", "boolean"}}},
                          {"sysex", {{"type", "boolean"}}}}}},
             .run = [&controller, title, direction, field](const Json& params) {
                 const auto id = trackParam(controller.project(), params);
                 const auto route =
                     mergeMidiRoute(controller, controller.project().track(id)->*field, params, direction);
                 return editTrack(controller, id, title, [&](model::Track& track) { track.*field = route; });
             }});
    };
    addMidiRoute("track.setMidiIn", "Set track MIDI input", Direction::input, &model::Track::midiIn);
    addMidiRoute("track.setMidiOut", "Set track MIDI output", Direction::output, &model::Track::midiOut);
}

}  // namespace

void registerTrackActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registerAudioKind(registry, controller, nav);
    registerMidiKind(registry, controller, nav);
    registry.addAction({.id = "track.remove",
                        .title = "Remove track",
                        .aliases = {"delete track"},
                        .category = "Tracks",
                        .schema = {{"required", {"track"}}},
                        .run = [&controller](const Json& params) {
                            controller.project().removeTrack(trackParam(controller.project(), params));
                            return projectState(controller);
                        }});
    registry.addAction({.id = "track.rename",
                        .title = "Rename track",
                        .category = "Tracks",
                        .schema = {{"required", {"track", "name"}}},
                        .run = [&controller](const Json& params) {
                            const auto id = trackParam(controller.project(), params);
                            const auto name = param<std::string>(params, "name");
                            return editTrack(controller, id, "rename track to " + name,
                                             [&](model::Track& track) { track.name = name; });
                        }});
    registry.addAction({.id = "track.move",
                        .title = "Move track",
                        .aliases = {"reorder track"},
                        .category = "Tracks",
                        .schema = {{"required", {"track", "index"}}},
                        .run = [&controller](const Json& params) {
                            const auto id = trackParam(controller.project(), params);
                            if (!controller.project().moveTrack(id, param<std::size_t>(params, "index"))) {
                                throw AppError("index must be below the track count");
                            }
                            return projectState(controller);
                        }});
    registry.addAction({.id = "track.select",
                        .title = "Select track",
                        .aliases = {"show track in inspector"},
                        .category = "Tracks",
                        .schema = {{"required", {"track"}}},
                        .run = [&controller, &nav](const Json& params) {
                            const auto id = trackParam(controller.project(), params);
                            nav.setSelectedTrack(qs(id.value));
                            return trackResult(controller, id);
                        }});

    for (const TrackFlag& flag : kTrackFlags) {
        registry.addAction({.id = std::string{flag.id},
                            .title = std::string{flag.title},
                            .aliases = {std::string{flag.alias}},
                            .category = "Tracks",
                            .schema = {{"required", {"track", "value"}}},
                            .run = [&controller, flag](const Json& params) {
                                const auto id = trackParam(controller.project(), params);
                                const bool value = param<bool>(params, "value");
                                Json result = editTrack(controller, id, std::string{flag.title},
                                                        [&](model::Track& track) { track.*flag.field = value; });
                                if (flag.live != nullptr) {
                                    (controller.*flag.live)(id, value);
                                }
                                return result;
                            }});
    }
    registry.addAction({.id = "track.setGain",
                        .title = "Set track gain",
                        .aliases = {"track volume", "fader"},
                        .category = "Tracks",
                        .schema = {{"required", {"track", "db"}}},
                        .run = [&controller](const Json& params) {
                            const auto id = trackParam(controller.project(), params);
                            const auto db = param<double>(params, "db");
                            Json result = editTrack(controller, id, "set track gain",
                                                    [&](model::Track& track) { track.gainDb = db; });
                            controller.setLiveGain(id, db);
                            return result;
                        }});
    registerRouteActions(registry, controller);
    registry.addAction({.id = "track.setIcon",
                        .title = "Set track icon",
                        .category = "Tracks",
                        .schema = {{"required", {"track", "icon"}}},
                        .run = [&controller](const Json& params) {
                            const auto id = trackParam(controller.project(), params);
                            const auto icon = param<std::string>(params, "icon");
                            return editTrack(controller, id, "set track icon",
                                             [&](model::Track& track) { track.icon = icon; });
                        }});
    registry.addAction({.id = "track.setColor",
                        .title = "Set track color",
                        .category = "Tracks",
                        .schema = {{"required", {"track", "color"}}},
                        .run = [&controller](const Json& params) {
                            const auto id = trackParam(controller.project(), params);
                            const auto color = param<int>(params, "color");
                            if (color < 0 || color >= kTrackColors) {
                                throw AppError("color must be 0 to " + std::to_string(kTrackColors - 1));
                            }
                            return editTrack(controller, id, "set track color",
                                             [&](model::Track& track) { track.color = color; });
                        }});
}

}  // namespace emerick::app
