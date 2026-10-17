#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "emerick/core/time.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

struct AudioFileInfo {
    core::Frames frames = 0;
    int sampleRate = 0;
    int channels = 0;
};

std::optional<AudioFileInfo> readAudioInfo(const std::filesystem::path& path);

// Writes one float WAV with a channel per vector; every vector has the same length. Throws AppError on failure.
void writeWav(const std::filesystem::path& path, const std::vector<std::vector<float>>& channels, double sampleRate);

// Imports and recorded takes both end here: a file outside <folder>/audio is copied in under a free name, and the
// clip spans the whole file. Throws AppError for an unreadable or empty file or another sample rate.
model::AudioClip clipFromFile(const std::filesystem::path& source, const std::filesystem::path& folder,
                              core::Ticks start, const core::TempoMap& tempo, double sampleRate);

}  // namespace emerick::app
