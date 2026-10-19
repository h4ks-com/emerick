#include "emerick/app/session_mapping.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

#include "emerick/core/overloaded.hpp"

namespace emerick::app {

namespace {

engine::AudioRoute toEngine(const model::AudioRoute& route) { return {route.deviceId, route.channels}; }

engine::MidiRoute toEngine(const model::MidiRoute& route) { return {route.portId, route.channel}; }

engine::AudioClip toEngine(const model::AudioClip& clip, const std::filesystem::path& folder) {
    const std::filesystem::path file{clip.file};
    return {.path = file.is_absolute() ? file : folder / file,
            .start = clip.start,
            .length = clip.length,
            .sourceOffset = clip.sourceOffset,
            .gain = gainFromDb(clip.gainDb),
            .fadeIn = clip.fadeIn,
            .fadeOut = clip.fadeOut};
}

engine::MidiClip toEngine(const model::MidiClip& clip) {
    // Older projects can hold events outside a trimmed clip, so we play only the ones inside it.
    const auto inside = [&](const model::MidiEvent& event) { return event.at >= 0 && event.at < clip.length; };
    const auto toEvent = [](const model::MidiEvent& event) { return engine::MidiEvent{event.at, event.bytes}; };
    return {.start = clip.start,
            .events = clip.events | std::views::filter(inside) | std::views::transform(toEvent) |
                      std::ranges::to<std::vector>()};
}

engine::Track commonFields(const model::Track& track, engine::TrackKind kind) {
    return {.id = track.id.value,
            .kind = kind,
            .armed = track.armed,
            .mute = track.mute,
            .solo = track.solo,
            .monitor = track.monitor,
            .gain = gainFromDb(track.gainDb),
            .audioIn = {},
            .audioOut = {},
            .midiIn = {},
            .midiOut = {},
            .midiThru = false,
            .claimsSysex = false,
            .audioClips = {},
            .midiClips = {}};
}

}  // namespace

float gainFromDb(double db) { return static_cast<float>(std::pow(10.0, db / 20.0)); }

engine::Track audioEngineTrack(const model::Track& track, const std::filesystem::path& folder) {
    engine::Track result = commonFields(track, engine::TrackKind::audio);
    result.audioIn = toEngine(track.audioIn);
    result.audioOut = toEngine(track.audioOut);
    for (const model::Clip& clip : track.clips) {
        std::visit(core::Overloaded{
                       [&](const model::AudioClip& audio) { result.audioClips.push_back(toEngine(audio, folder)); },
                       [](const model::MidiClip&) {}},
                   clip);
    }
    return result;
}

engine::Track midiEngineTrack(const model::Track& track) {
    engine::Track result = commonFields(track, engine::TrackKind::midi);
    result.midiIn = toEngine(track.midiIn);
    result.midiOut = toEngine(track.midiOut);
    result.midiThru = track.midiOut.thru;
    result.claimsSysex = track.midiIn.sysex;
    for (const model::Clip& clip : track.clips) {
        std::visit(core::Overloaded{[](const model::AudioClip&) {},
                                    [&](const model::MidiClip& midi) { result.midiClips.push_back(toEngine(midi)); }},
                   clip);
    }
    return result;
}

engine::Session toSession(const model::Document& document, const Registry& registry,
                          const std::filesystem::path& folder) {
    engine::Session session{
        .tempo = document.tempo, .tracks = {}, .clockPorts = document.clockPorts, .takeFolder = folder / "audio"};
    const PlaybackContext context{.tempo = document.tempo, .folder = folder};
    for (const model::Track& track : document.tracks) {
        const TrackType* type = registry.trackType(track.kind);
        if (type != nullptr && type->toEngine) {
            session.tracks.push_back(type->toEngine(track, context));
        }
    }
    return session;
}

std::vector<engine::SessionError> dropMissingDevices(engine::Session& session,
                                                     std::span<const engine::AudioDeviceInfo> devices) {
    std::vector<engine::SessionError> errors;
    const auto drop = [&](const engine::Track& track, engine::AudioRoute& route, const char* side) {
        if (!route.deviceId.empty() && !std::ranges::contains(devices, route.deviceId, &engine::AudioDeviceInfo::id)) {
            errors.push_back({track.id, std::string{"the audio "} + side + " device " + route.deviceId +
                                            " is missing, pick a device in the inspector"});
            route = {};
        }
    };
    for (engine::Track& track : session.tracks) {
        drop(track, track.audioIn, "input");
        drop(track, track.audioOut, "output");
    }
    return errors;
}

void repairDeviceIds(model::Document& document, std::span<const engine::AudioDeviceInfo> devices) {
    const auto repair = [&](model::AudioRoute& route) {
        if (route.deviceId.empty() || std::ranges::contains(devices, route.deviceId, &engine::AudioDeviceInfo::id)) {
            return;
        }
        const auto known = std::ranges::find_if(
            devices, [&](const engine::AudioDeviceInfo& device) { return route.deviceId.starts_with(device.id); });
        if (known != devices.end()) {
            route.deviceId = known->id;
        }
    };
    for (model::Track& track : document.tracks) {
        repair(track.audioIn);
        repair(track.audioOut);
    }
}

}  // namespace emerick::app
