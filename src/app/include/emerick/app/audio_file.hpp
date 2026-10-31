#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
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

struct AudioData {
    std::vector<std::vector<float>> channels;
    double sampleRate = 0;
};

// Reads every channel of a WAV, AIFF or other file libsndfile opens. Throws AppError when it cannot.
AudioData readAudio(const std::filesystem::path& path);

// Writes one float WAV chunk by chunk, with a channel per vector of each chunk. Throws AppError on failure.
class WavWriter {
public:
    WavWriter(const std::filesystem::path& path, std::size_t channels, double sampleRate);
    ~WavWriter();
    WavWriter(WavWriter&&) noexcept;
    WavWriter& operator=(WavWriter&&) noexcept;
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    // Appends the chunk; every vector has the same length.
    void write(std::span<const std::vector<float>> channels);

private:
    struct File;
    std::unique_ptr<File> file_;
};

// Writes one float WAV with a channel per vector; every vector has the same length. Throws AppError on failure.
void writeWav(const std::filesystem::path& path, const std::vector<std::vector<float>>& channels, double sampleRate);

// Imports and recorded takes both end here: a file outside <folder>/audio is copied in under a free name, and the
// clip spans the whole file. Throws AppError for an unreadable or empty file or another sample rate.
model::AudioClip clipFromFile(const std::filesystem::path& source, const std::filesystem::path& folder,
                              core::Ticks start, const core::TempoMap& tempo, double sampleRate);

}  // namespace emerick::app
