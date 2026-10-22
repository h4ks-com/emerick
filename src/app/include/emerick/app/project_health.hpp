#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "emerick/app/enums.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/engine/audio_backend.hpp"
#include "emerick/engine/midi_backend.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// The clip id is empty for a problem with the whole track, such as a route to a missing device.
struct HealthIssue {
    HealthProblem problem = HealthProblem::MissingFile;
    std::string track;
    std::string clip;
    std::string message;
};

struct HealthContext {
    std::filesystem::path folder;
    double sampleRate = 0.0;
    std::span<const engine::AudioDeviceInfo> audioDevices;
    std::span<const engine::MidiPortInfo> midiPorts;
};

std::vector<HealthIssue> checkHealth(const model::Document& document, const HealthContext& context);
Json healthJson(const std::vector<HealthIssue>& issues);

}  // namespace emerick::app
