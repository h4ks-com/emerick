#include "emerick/app/audio_file.hpp"

#include <sndfile.h>

#include <cmath>
#include <memory>
#include <string>

#include "emerick/app/registry.hpp"

namespace emerick::app {

namespace {

struct SndFileCloser {
    void operator()(SNDFILE* file) const { sf_close(file); }
};
using SndFile = std::unique_ptr<SNDFILE, SndFileCloser>;

std::filesystem::path freePath(const std::filesystem::path& folder, const std::filesystem::path& name) {
    std::filesystem::path path = folder / name.filename();
    for (int number = 2; std::filesystem::exists(path); ++number) {
        path = folder / (name.stem().string() + "-" + std::to_string(number) + name.extension().string());
    }
    return path;
}

std::filesystem::path copyIntoProject(const std::filesystem::path& source, const std::filesystem::path& folder) {
    const std::filesystem::path audio = folder / "audio";
    if (std::filesystem::absolute(source).parent_path() == std::filesystem::absolute(audio)) {
        return source;
    }
    const std::filesystem::path target = freePath(audio, source);
    try {
        std::filesystem::create_directories(audio);
        std::filesystem::copy_file(source, target);
    } catch (const std::filesystem::filesystem_error& error) {
        throw AppError(error.what());
    }
    return target;
}

}  // namespace

std::optional<AudioFileInfo> readAudioInfo(const std::filesystem::path& path) {
    SF_INFO info{};
    const SndFile file{sf_open(path.c_str(), SFM_READ, &info)};
    if (!file) {
        return std::nullopt;
    }
    return AudioFileInfo{info.frames, info.samplerate, info.channels};
}

void writeWav(const std::filesystem::path& path, const std::vector<std::vector<float>>& channels, double sampleRate) {
    SF_INFO info{.frames = 0,
                 .samplerate = static_cast<int>(std::lround(sampleRate)),
                 .channels = static_cast<int>(channels.size()),
                 .format = SF_FORMAT_WAV | SF_FORMAT_FLOAT,
                 .sections = 0,
                 .seekable = 0};
    const SndFile file{sf_open(path.c_str(), SFM_WRITE, &info)};
    if (!file) {
        throw AppError("cannot write " + path.string() + ": " + sf_strerror(nullptr));
    }
    const std::size_t frames = channels.empty() ? 0 : channels.front().size();
    std::vector<float> interleaved(frames * channels.size());
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            interleaved[frame * channels.size() + channel] = channels[channel][frame];
        }
    }
    if (sf_writef_float(file.get(), interleaved.data(), static_cast<sf_count_t>(frames)) !=
        static_cast<sf_count_t>(frames)) {
        throw AppError("cannot write " + path.string());
    }
}

model::AudioClip clipFromFile(const std::filesystem::path& source, const std::filesystem::path& folder,
                              core::Ticks start, const core::TempoMap& tempo, double sampleRate) {
    const auto info = readAudioInfo(source);
    if (!info) {
        throw AppError("cannot open " + source.filename().string() + " as audio: " + sf_strerror(nullptr));
    }
    if (info->frames == 0) {
        throw AppError(source.filename().string() + " holds no audio");
    }
    if (info->sampleRate != std::lround(sampleRate)) {
        throw AppError("the file runs at " + std::to_string(info->sampleRate) + " Hz and the project at " +
                       std::to_string(std::lround(sampleRate)) + " Hz, convert it first");
    }
    const std::filesystem::path file = copyIntoProject(source, folder);
    model::AudioClip clip;
    clip.start = start;
    clip.length = tempo.framesToTicks(info->frames, info->sampleRate);
    clip.file = std::filesystem::relative(file, folder).generic_string();
    return clip;
}

}  // namespace emerick::app
