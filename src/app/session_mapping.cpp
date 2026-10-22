#include "emerick/app/session_mapping.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "emerick/core/overloaded.hpp"
#include "emerick/model/midi_notes.hpp"

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
            .fadeOut = clip.fadeOut,
            .fadeInCurve = core::fadeCurve(clip.fadeInShape, clip.fadeInCurve),
            .fadeOutCurve = core::fadeCurve(clip.fadeOutShape, clip.fadeOutCurve)};
}

engine::MidiClip toEngine(const model::MidiClip& clip) {
    const auto toEvent = [](const model::MidiEvent& event) { return engine::MidiEvent{event.at, event.bytes}; };
    return {.start = clip.start,
            .events = model::clipEvents(clip) | std::views::transform(toEvent) | std::ranges::to<std::vector>()};
}

engine::Track commonFields(const model::Track& track, engine::TrackKind kind) {
    return {.id = track.id.value,
            .kind = kind,
            .armed = track.armed,
            .mute = track.mute,
            .solo = track.solo,
            .monitor = track.monitor,
            .gain = gainFromDb(track.gainDb),
            .pan = static_cast<float>(track.pan),
            .audioIn = {},
            .audioOut = {},
            .midiIn = {},
            .midiOut = {},
            .midiThru = false,
            .claimsSysex = false,
            .outputBus = {},
            .sends = {},
            .audioClips = {},
            .midiClips = {}};
}

// Disabled sends stay in the model so they keep their level, and play nothing.
void addBusRoutes(engine::Track& result, const model::Track& track) {
    result.audioOut = toEngine(track.audioOut);
    result.outputBus = track.outputBus.value;
    result.sends = track.sends | std::views::filter(&model::Send::enabled) |
                   std::views::transform([](const model::Send& send) {
                       return engine::Send{.bus = send.bus.value, .gain = gainFromDb(send.levelDb), .pre = send.pre};
                   }) |
                   std::ranges::to<std::vector>();
}

}  // namespace

float gainFromDb(double db) { return static_cast<float>(std::pow(10.0, db / 20.0)); }

engine::PanLaw panLawFromSettings(const Registry& registry) {
    if (registry.setting(kPanLawSetting) == nullptr) {
        return engine::PanLaw::compensated;
    }
    const Json value = registry.settingValue(kPanLawSetting);
    const auto* const found =
        std::ranges::find(kPanLaws, value, [](const NamedPanLaw& entry) { return Json(entry.name); });
    return found == kPanLaws.end() ? engine::PanLaw::compensated : found->law;
}

engine::Track audioEngineTrack(const model::Track& track, const std::filesystem::path& folder) {
    engine::Track result = commonFields(track, engine::TrackKind::audio);
    result.audioIn = toEngine(track.audioIn);
    addBusRoutes(result, track);
    for (const model::Clip& clip : track.clips) {
        std::visit(core::Overloaded{
                       [&](const model::AudioClip& audio) { result.audioClips.push_back(toEngine(audio, folder)); },
                       [](const model::MidiClip&) {}},
                   clip);
    }
    return result;
}

engine::Track busEngineTrack(const model::Track& track) {
    engine::Track result = commonFields(track, engine::TrackKind::bus);
    addBusRoutes(result, track);
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
    engine::Session session{.tempo = document.tempo,
                            .tracks = {},
                            .clockPorts = document.clockPorts,
                            .takeFolder = folder / "audio",
                            .metronome = {},
                            .panLaw = panLawFromSettings(registry)};
    const PlaybackContext context{.tempo = document.tempo, .folder = folder};
    for (const model::Track& track : document.tracks) {
        const TrackType* type = registry.trackType(track.kind);
        if (type != nullptr && type->toEngine) {
            session.tracks.push_back(type->toEngine(track, context));
        }
    }
    return session;
}

std::vector<engine::SessionError> dropUnusableDevices(engine::Session& session,
                                                      std::span<const engine::AudioDeviceInfo> devices,
                                                      double sampleRate) {
    std::vector<engine::SessionError> errors;
    const auto drop = [&](const std::string& owner, engine::AudioRoute& route, const std::string& missingText) {
        if (route.deviceId.empty()) {
            return;
        }
        const auto device = std::ranges::find(devices, route.deviceId, &engine::AudioDeviceInfo::id);
        auto problem =
            device == devices.end() ? std::optional{missingText} : engine::unsupportedRate(*device, sampleRate);
        if (problem) {
            errors.push_back({owner, std::move(*problem)});
            route = {};
        }
    };
    for (engine::Track& track : session.tracks) {
        for (auto [route, side] : {std::pair{&track.audioIn, "input"}, std::pair{&track.audioOut, "output"}}) {
            drop(track.id, *route,
                 std::string{"the audio "} + side + " device " + route->deviceId +
                     " is missing, pick a device in the inspector");
        }
    }
    engine::AudioRoute& metronome = session.metronome.audioOut;
    drop(engine::kMetronomeId, metronome,
         "the metronome output device " + metronome.deviceId + " is missing, so it plays on the main output");
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
