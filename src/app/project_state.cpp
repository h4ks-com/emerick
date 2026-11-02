#include "emerick/app/project_state.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <variant>

#include "emerick/core/overloaded.hpp"
#include "emerick/engine/effects.hpp"
#include "emerick/model/automation.hpp"
#include "emerick/model/controls.hpp"
#include "emerick/model/midi_notes.hpp"

namespace emerick::app {

namespace {

constexpr const char* kNoRoute = "None";

Json channelChoices(const engine::AudioDeviceInfo& device, int count) {
    Json choices = Json::array();
    for (int channel = 1; channel <= count; ++channel) {
        choices.push_back(
            {{"device", device.id}, {"channels", {channel}}, {"label", device.name + " " + std::to_string(channel)}});
    }
    for (int channel = 1; channel < count; channel += 2) {
        choices.push_back({{"device", device.id},
                           {"channels", {channel, channel + 1}},
                           {"label", device.name + " " + std::to_string(channel) + "-" + std::to_string(channel + 1)}});
    }
    return choices;
}

// Mono channels first, then stereo pairs, each with the params that select it.
Json channelOptions(int count) {
    Json options = Json::array();
    for (int channel = 1; channel <= count; ++channel) {
        options.push_back({{"id", std::to_string(channel)},
                           {"name", std::to_string(channel)},
                           {"params", {{"channels", {channel}}}}});
    }
    for (int channel = 1; channel < count; channel += 2) {
        const std::string pair = std::to_string(channel) + "-" + std::to_string(channel + 1);
        options.push_back({{"id", pair}, {"name", pair}, {"params", {{"channels", {channel, channel + 1}}}}});
    }
    return options;
}

Json audioDevices(const Json& devices, const char* channelsKey) {
    Json result =
        Json::array({{{"id", ""}, {"name", kNoRoute}, {"params", {{"device", ""}}}, {"channels", Json::array()}}});
    for (const Json& device : devices) {
        const int count = device.at(channelsKey);
        if (count > 0) {
            result.push_back({{"id", device.at("id")},
                              {"name", device.at("name")},
                              {"params", {{"device", device.at("id")}}},
                              {"channels", channelOptions(count)}});
        }
    }
    return result;
}

Json trackErrors(const std::vector<engine::SessionError>& errors) {
    return errors | std::views::transform([](const engine::SessionError& error) {
               return Json{{"track", error.trackId}, {"message", error.message}};
           }) |
           std::ranges::to<Json::array_t>();
}

Json midiPorts(const Json& ports) {
    Json result = Json::array({{{"id", ""}, {"name", kNoRoute}, {"params", {{"port", ""}}}}});
    for (const Json& port : ports) {
        result.push_back({{"id", port.at("port")}, {"name", port.at("name")}, {"params", {{"port", port.at("port")}}}});
    }
    return result;
}

Json midiChannels() {
    Json result = Json::array({{{"id", "0"}, {"name", "All"}, {"params", {{"channel", 0}}}}});
    for (int channel = 1; channel <= kMidiChannels; ++channel) {
        result.push_back(
            {{"id", std::to_string(channel)}, {"name", std::to_string(channel)}, {"params", {{"channel", channel}}}});
    }
    return result;
}

Json timing(core::Ticks start, core::Ticks length, const core::TempoMap& tempo) {
    return {{"start", start},
            {"startBbt", tempo.toBarBeatTick(start).format()},
            {"length", length},
            {"lengthBbt", durationText(length, tempo)},
            {"endBbt", tempo.toBarBeatTick(start + length).format()}};
}

// The text names the device the way the pickers do, such as "MOTU M2 1-2"; the device id stays for actions.
Json audioRoute(const model::AudioRoute& route, std::span<const engine::AudioDeviceInfo> devices) {
    return {{"device", route.deviceId}, {"channels", route.channels}, {"text", audioRouteText(route, devices)}};
}

Json midiRoute(const model::MidiRoute& route) {
    Json json{{"port", route.portId}, {"channel", route.channel}, {"thru", route.thru}, {"sysex", route.sysex}};
    if (!route.sourceTrack.empty()) {
        json["sourceTrack"] = route.sourceTrack.value;
    }
    return json;
}

Json syncState(const ProjectController& controller) {
    Json badges = Json::array();
    for (const auto& [source, status] : controller.syncStatus()) {
        badges.push_back({{"source", source}, {"label", status.label}, {"detail", status.detail}, {"ok", status.ok}});
    }
    return badges;
}

}  // namespace

std::string audioRouteText(const model::AudioRoute& route, std::span<const engine::AudioDeviceInfo> devices) {
    if (route.deviceId.empty()) {
        return kNoRoute;
    }
    const auto device = std::ranges::find(devices, route.deviceId, &engine::AudioDeviceInfo::id);
    std::string text = device != devices.end() ? device->name : route.deviceId;
    for (std::size_t index = 0; index < route.channels.size(); ++index) {
        text += (index == 0 ? " " : "-") + std::to_string(route.channels[index]);
    }
    return text;
}

std::string durationText(core::Ticks ticks, const core::TempoMap& tempo) {
    const core::BarBeatTick position = tempo.toBarBeatTick(ticks);
    return core::BarBeatTick{position.bar - 1, position.beat - 1, position.tick}.format();
}

Json positionState(core::Ticks ticks, const core::TempoMap& tempo) {
    return {{"ticks", ticks}, {"bbt", tempo.toBarBeatTick(ticks).format()}, {"seconds", tempo.ticksToSeconds(ticks)}};
}

Json noteState(const model::Note& note) {
    return {{"id", note.id.value}, {"pitch", note.pitch},   {"velocity", note.velocity},
            {"start", note.start}, {"length", note.length}, {"channel", note.channel}};
}

Json clipState(const model::Clip& clip, const core::TempoMap& tempo, bool includeEvents) {
    return std::visit(
        core::Overloaded{[&](const model::AudioClip& audio) {
                             Json result{{"id", audio.id.value}, {"type", "audio"}};
                             result.update(timing(audio.start, audio.length, tempo));
                             result.update({{"file", audio.file},
                                            {"sourceOffset", audio.sourceOffset},
                                            {"gainDb", audio.gainDb},
                                            {"fadeIn", audio.fadeIn},
                                            {"fadeOut", audio.fadeOut},
                                            {"fadeInShape", core::fadeShapeId(audio.fadeInShape)},
                                            {"fadeOutShape", core::fadeShapeId(audio.fadeOutShape)},
                                            {"fadeInCurve", audio.fadeInCurve},
                                            {"fadeOutCurve", audio.fadeOutCurve},
                                            {"muted", audio.muted}});
                             return result;
                         },
                         [&](const model::MidiClip& midi) {
                             Json result{{"id", midi.id.value}, {"type", "midi"}};
                             result.update(timing(midi.start, midi.length, tempo));
                             result["muted"] = midi.muted;
                             result["loopLength"] = model::looped(midi) ? midi.loopLength : 0;
                             result["loopEndBbt"] = tempo.toBarBeatTick(midi.start + model::clipSpan(midi)).format();
                             result["noteCount"] = midi.notes.size();
                             result["eventCount"] = midi.events.size();
                             if (includeEvents) {
                                 result["notes"] =
                                     midi.notes | std::views::transform(noteState) | std::ranges::to<Json::array_t>();
                                 result["events"] = Json::array();
                                 for (const model::MidiEvent& event : midi.events) {
                                     result["events"].push_back({{"at", event.at}, {"bytes", event.bytes}});
                                 }
                                 result["controls"] = Json::object();
                                 for (const model::Control control : model::controlsIn(midi)) {
                                     result["controls"][model::controlId(control)] =
                                         model::controlPoints(midi, control) |
                                         std::views::transform([](const model::ControlPoint& point) {
                                             return Json{{"at", point.at}, {"value", point.value}};
                                         }) |
                                         std::ranges::to<Json::array_t>();
                                 }
                             }
                             return result;
                         }},
        clip);
}

Json insertState(const model::Insert& insert) {
    const engine::EffectSpec* spec = engine::findEffect(insert.type);
    Json state{{"id", insert.id.value},
               {"type", insert.type},
               {"title", spec != nullptr ? std::string{spec->title} : insert.type},
               {"bypass", insert.bypass},
               {"params", insert.params}};
    if (insert.plugin) {
        state["title"] = insert.plugin->name;
        state["plugin"] = {{"id", insert.plugin->pluginId},
                           {"format", core::pluginFormatName(insert.plugin->format).id},
                           {"name", insert.plugin->name},
                           {"bundle", insert.plugin->bundle}};
    }
    return state;
}

Json trackState(const model::Track& track, const core::TempoMap& tempo,
                std::span<const engine::AudioDeviceInfo> devices) {
    Json clips = Json::array();
    for (const model::Clip& clip : track.clips) {
        clips.push_back(clipState(clip, tempo));
    }
    return {
        {"id", track.id.value},
        {"name", track.name},
        {"kind", track.kind},
        {"icon", track.icon},
        {"color", track.color},
        {"mute", track.mute},
        {"solo", track.solo},
        {"armed", track.armed},
        {"monitor", track.monitor},
        {"gainDb", track.gainDb},
        {"trimDb", track.trimDb},
        {"pan", track.pan},
        {"audioIn", audioRoute(track.audioIn, devices)},
        {"audioOut", audioRoute(track.audioOut, devices)},
        {"midiIn", midiRoute(track.midiIn)},
        {"midiOut", midiRoute(track.midiOut)},
        {"outputBus", track.outputBus.value},
        {"sends", track.sends | std::views::transform([](const model::Send& send) {
                      return Json{
                          {"bus", send.bus.value}, {"db", send.levelDb}, {"pre", send.pre}, {"enabled", send.enabled}};
                  }) | std::ranges::to<Json::array_t>()},
        {"inserts", track.inserts | std::views::transform(insertState) | std::ranges::to<Json::array_t>()},
        {"clips", std::move(clips)},
        {"automationMode", model::automationModeId(track.automationMode)},
        {"automation", track.automation | std::views::transform([](const model::AutomationLane& lane) {
                           return Json{{"param", lane.param}, {"pointCount", lane.points.size()}};
                       }) | std::ranges::to<Json::array_t>()},
        {"recordDelayMs", track.recordDelayMs},
        {"data", track.data}};
}

Json tempoState(const core::TempoMap& tempo) {
    return {{"bpm", tempo.bpm}, {"numerator", tempo.meter.numerator}, {"denominator", tempo.meter.denominator}};
}

Json markerState(const model::Marker& marker, int number, const core::TempoMap& tempo) {
    return {{"id", marker.id.value},
            {"name", marker.name},
            {"number", number},
            {"at", marker.at},
            {"position", tempo.toBarBeatTick(marker.at).format()},
            {"color", marker.color ? Json(*marker.color) : Json(nullptr)}};
}

Json markersState(const model::Document& document) {
    return std::views::zip(std::views::iota(1), document.markers) | std::views::transform([&](const auto& entry) {
               return markerState(std::get<1>(entry), std::get<0>(entry), document.tempo);
           }) |
           std::ranges::to<Json::array_t>();
}

Json projectState(ProjectController& controller) {
    controller.flush();
    const model::Project& project = controller.project();
    const model::Document& document = project.document();
    Json result{{"path", controller.folder().string()},
                {"untitled", controller.untitled()},
                {"selfContained", document.selfContained},
                {"tempo", tempoState(document.tempo)},
                {"clockOut", document.clockPorts},
                {"markers", markersState(document)},
                {"undo", project.undoName() ? Json(*project.undoName()) : Json(nullptr)},
                {"redo", project.redoName() ? Json(*project.redoName()) : Json(nullptr)},
                {"tracks", document.tracks | std::views::transform([&](const model::Track& track) {
                               return trackState(track, document.tempo, controller.engine().audioDevices());
                           }) | std::ranges::to<Json::array_t>()}};
    if (!controller.sessionError().empty()) {
        result["sessionError"] = controller.sessionError();
    }
    if (!controller.engine().sessionErrors().empty()) {
        result["skippedClips"] = trackErrors(controller.engine().sessionErrors());
    }
    if (!controller.takeErrors().empty()) {
        result["takeErrors"] = trackErrors(controller.takeErrors());
    }
    if (!controller.routeErrors().empty()) {
        result["routeErrors"] = trackErrors(controller.routeErrors());
    }
    return result;
}

Json streamState(const engine::Engine& engine) {
    const engine::StreamLayout& layout = engine.layout();
    const auto milliseconds = [&](engine::Frames frames) {
        return std::lround(static_cast<double>(frames) * 1000.0 / layout.sampleRate);
    };
    Json devices = Json::array();
    for (const engine::DeviceChannels& device : layout.devices) {
        const auto info = std::ranges::find(engine.audioDevices(), device.id, &engine::AudioDeviceInfo::id);
        devices.push_back({{"id", device.id},
                           {"name", info != engine.audioDevices().end() ? info->name : device.id},
                           {"firstOutput", device.firstOutput + 1},
                           {"outputs", device.outputs},
                           {"firstInput", device.firstInput + 1},
                           {"inputs", device.inputs}});
    }
    return {{"open", engine.streamOpen()},
            {"sampleRate", layout.sampleRate},
            {"blockFrames", layout.maxFrames},
            {"inputLatencyMs", milliseconds(layout.inputLatency)},
            {"outputLatencyMs", milliseconds(layout.outputLatency)},
            {"pluginDelayFrames", engine.latencyCompensation()},
            {"pluginDelayMs", milliseconds(engine.latencyCompensation())},
            {"devices", devices}};
}

Json cycleState(const model::Document& document) {
    return {{"enabled", document.cycle.enabled},
            {"start", positionState(document.cycle.start, document.tempo)},
            {"end", positionState(document.cycle.end, document.tempo)}};
}

Json transportState(const ProjectController& controller) {
    return {{"playing", controller.playing()},
            {"recording", controller.recording()},
            {"position", positionState(controller.positionTicks(), controller.project().document().tempo)},
            {"streamError", controller.streamError().toStdString()},
            {"startReason", controller.startReason().toStdString()},
            {"cycle", cycleState(controller.project().document())},
            {"sync", syncState(controller)},
            {"meters", controller.meters().state()},
            {"stream", streamState(controller.engine())}};
}

Json midiOutputChoices(const ProjectController& controller) {
    return controller.engine().midiPorts() |
           std::views::filter([](const engine::MidiPortInfo& port) { return !port.isInput; }) |
           std::views::transform(
               [](const engine::MidiPortInfo& port) { return Json{{"id", port.id}, {"name", port.name}}; }) |
           std::ranges::to<Json::array_t>();
}

Json devicesState(const ProjectController& controller) {
    Json audio = Json::array();
    Json inputs = Json::array();
    Json outputs = Json::array();
    for (const engine::AudioDeviceInfo& device : controller.engine().audioDevices()) {
        audio.push_back({{"id", device.id},
                         {"name", device.name},
                         {"inputs", device.inputChannels},
                         {"outputs", device.outputChannels},
                         {"sampleRates", device.sampleRates}});
        for (Json& choice : channelChoices(device, device.inputChannels)) {
            inputs.push_back(std::move(choice));
        }
        for (Json& choice : channelChoices(device, device.outputChannels)) {
            outputs.push_back(std::move(choice));
        }
    }
    Json midiInputs = Json::array();
    Json midiOutputs = Json::array();
    for (const engine::MidiPortInfo& port : controller.engine().midiPorts()) {
        (port.isInput ? midiInputs : midiOutputs).push_back({{"port", port.id}, {"name", port.name}});
    }
    return {{"audio", audio},
            {"audioInputs", inputs},
            {"audioOutputs", outputs},
            {"midiInputs", midiInputs},
            {"midiOutputs", midiOutputs}};
}

std::string latencyText(const engine::StreamLayout& layout) {
    constexpr double kMsPerSecond = 1000.0;
    const auto ms = [&](core::Frames frames) { return static_cast<double>(frames) * kMsPerSecond / layout.sampleRate; };
    return std::format("{:.1f} ms round trip, {:.1f} ms output", ms(layout.inputLatency + layout.outputLatency),
                       ms(layout.outputLatency));
}

Json routeDevices(const Json& devices) {
    return {{"audioIn", audioDevices(devices.at("audio"), "inputs")},
            {"audioOut", audioDevices(devices.at("audio"), "outputs")},
            {"midiIn", midiPorts(devices.at("midiInputs"))},
            {"midiOut", midiPorts(devices.at("midiOutputs"))},
            {"midiChannels", midiChannels()}};
}

}  // namespace emerick::app
