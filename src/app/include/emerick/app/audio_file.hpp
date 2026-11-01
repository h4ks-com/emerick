#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

enum class AudioFormat : std::uint8_t { Wav, Flac, Ogg, Opus, Mp3 };

// Each format by the name an export takes and the suffix of its files.
inline constexpr std::array<std::pair<std::string_view, AudioFormat>, 5> kAudioFormats{{{"wav", AudioFormat::Wav},
                                                                                        {"flac", AudioFormat::Flac},
                                                                                        {"ogg", AudioFormat::Ogg},
                                                                                        {"opus", AudioFormat::Opus},
                                                                                        {"mp3", AudioFormat::Mp3}}};

std::string_view audioFormatName(AudioFormat format);

// WAV holds 32-bit floats and FLAC 24-bit integers. Quality from 0 to 1 sets the lossy formats' variable bitrate.
struct AudioEncoding {
    AudioFormat format = AudioFormat::Wav;
    double quality = 0.6;
};

// The format named by format, else by the path's suffix, at quality or the default. Throws AppError listing the
// formats for an unknown one, and for a quality outside 0 to 1.
AudioEncoding audioEncoding(const std::filesystem::path& path, const std::optional<std::string>& format,
                            std::optional<double> quality);

// Writes one audio file chunk by chunk, with a channel per vector of each chunk. Throws AppError on failure, and
// before it creates the file when this libsndfile cannot encode the format at that rate.
class AudioWriter {
public:
    AudioWriter(const std::filesystem::path& path, std::size_t channels, double sampleRate,
                AudioEncoding encoding = {});
    ~AudioWriter();
    AudioWriter(AudioWriter&&) noexcept;
    AudioWriter& operator=(AudioWriter&&) noexcept;
    AudioWriter(const AudioWriter&) = delete;
    AudioWriter& operator=(const AudioWriter&) = delete;

    // Appends the chunk; every vector has the same length.
    void write(std::span<const std::vector<float>> channels);

private:
    struct File;
    std::unique_ptr<File> file_;
};

// Writes one float WAV with a channel per vector; every vector has the same length. Throws AppError on failure.
void writeWav(const std::filesystem::path& path, const std::vector<std::vector<float>>& channels, double sampleRate);
void writeAudio(const std::filesystem::path& path, const std::vector<std::vector<float>>& channels, double sampleRate,
                AudioEncoding encoding);

// Imports and recorded takes both end here: a file outside <folder>/audio is copied in under a free name, and the
// clip spans the whole file. Throws AppError for an unreadable or empty file or another sample rate.
model::AudioClip clipFromFile(const std::filesystem::path& source, const std::filesystem::path& folder,
                              core::Ticks start, const core::TempoMap& tempo, double sampleRate);

}  // namespace emerick::app
