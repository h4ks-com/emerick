#include "emerick/app/project_health.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "emerick/app/audio_file.hpp"
#include "emerick/core/overloaded.hpp"

namespace emerick::app {

namespace {

// Clip lengths round to whole ticks, so a clip that spans its whole file can end one tick past it.
constexpr core::Ticks kRoundingTicks = 1;

HealthIssue issue(HealthProblem problem, const model::Track& track, const std::string& clip, const std::string& text) {
    return {.problem = problem, .track = track.id.value, .clip = clip, .message = track.name + ": " + text};
}

void checkAudioClip(const model::Track& track, const model::AudioClip& clip, const core::TempoMap& tempo,
                    const HealthContext& context, std::vector<HealthIssue>& issues) {
    const std::filesystem::path file{clip.file};
    const std::optional<AudioFileInfo> info = readAudioInfo(file.is_absolute() ? file : context.folder / file);
    if (!info) {
        issues.push_back(issue(HealthProblem::MissingFile, track, clip.id.value,
                               clip.file + " is missing, put the file back or delete the clip"));
        return;
    }
    const core::Ticks fileEnd = tempo.framesToTicks(info->frames, info->sampleRate);
    if (tempo.framesToTicks(clip.sourceOffset, info->sampleRate) + clip.length > fileEnd + kRoundingTicks) {
        issues.push_back(issue(HealthProblem::ClipPastSource, track, clip.id.value,
                               "the clip runs past the end of " + clip.file + ", shorten the clip"));
    }
    const long projectRate = std::lround(context.sampleRate);
    if (info->sampleRate != projectRate) {
        issues.push_back(issue(HealthProblem::SampleRate, track, clip.id.value,
                               clip.file + " runs at " + std::to_string(info->sampleRate) + " Hz and the project at " +
                                   std::to_string(projectRate) + " Hz, convert the file"));
    }
}

void checkAudioRoute(const model::Track& track, const model::AudioRoute& route, const std::string& role,
                     const HealthContext& context, std::vector<HealthIssue>& issues) {
    if (!route.deviceId.empty() && std::ranges::find(context.audioDevices, route.deviceId,
                                                     &engine::AudioDeviceInfo::id) == context.audioDevices.end()) {
        issues.push_back(issue(HealthProblem::MissingDevice, track, {},
                               "the audio " + role + " " + route.deviceId + " is gone, connect it or pick another"));
    }
}

void checkMidiRoute(const model::Track& track, const model::MidiRoute& route, bool input, const HealthContext& context,
                    std::vector<HealthIssue>& issues) {
    if (!route.portId.empty() && std::ranges::none_of(context.midiPorts, [&](const engine::MidiPortInfo& port) {
            return port.id == route.portId && port.isInput == input;
        })) {
        issues.push_back(issue(HealthProblem::MissingDevice, track, {},
                               std::string{"the MIDI "} + (input ? "input " : "output ") + route.portId +
                                   " is gone, connect it or pick another"));
    }
}

}  // namespace

std::vector<HealthIssue> checkHealth(const model::Document& document, const HealthContext& context) {
    std::vector<HealthIssue> issues;
    for (const model::Track& track : document.tracks) {
        for (const model::Clip& clip : track.clips) {
            std::visit(core::Overloaded{[&](const model::AudioClip& audio) {
                                            checkAudioClip(track, audio, document.tempo, context, issues);
                                        },
                                        [](const model::MidiClip&) {}},
                       clip);
        }
        checkAudioRoute(track, track.audioIn, "input", context, issues);
        checkAudioRoute(track, track.audioOut, "output", context, issues);
        checkMidiRoute(track, track.midiIn, true, context, issues);
        checkMidiRoute(track, track.midiOut, false, context, issues);
    }
    return issues;
}

Json healthJson(const std::vector<HealthIssue>& issues) {
    Json result = Json::array();
    for (const HealthIssue& entry : issues) {
        Json item{{"problem", enumKey(entry.problem)}, {"track", entry.track}, {"message", entry.message}};
        if (!entry.clip.empty()) {
            item["clip"] = entry.clip;
        }
        result.push_back(std::move(item));
    }
    return result;
}

}  // namespace emerick::app
