#include "emerick/app/audio_file.hpp"

#include <sndfile.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
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

AudioData readAudio(const std::filesystem::path& path) {
    SF_INFO info{};
    const SndFile file{sf_open(path.c_str(), SFM_READ, &info)};
    if (!file) {
        throw AppError("cannot open " + path.filename().string() + " as audio: " + sf_strerror(nullptr));
    }
    const auto channels = static_cast<std::size_t>(info.channels);
    std::vector<float> interleaved(static_cast<std::size_t>(info.frames) * channels);
    const auto frames = static_cast<std::size_t>(sf_readf_float(file.get(), interleaved.data(), info.frames));
    AudioData audio{.channels = std::vector<std::vector<float>>(channels, std::vector<float>(frames)),
                    .sampleRate = static_cast<double>(info.samplerate)};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            audio.channels[channel][frame] = interleaved[(frame * channels) + channel];
        }
    }
    return audio;
}

std::string_view audioFormatName(AudioFormat format) {
    return std::ranges::find(kAudioFormats, format, &std::pair<std::string_view, AudioFormat>::second)->first;
}

AudioEncoding audioEncoding(const std::filesystem::path& path, const std::optional<std::string>& format,
                            std::optional<double> quality) {
    std::string name = format.value_or(path.extension().string());
    if (!format) {
        name = name.empty() ? std::string{audioFormatName(AudioFormat::Wav)} : name.substr(1);
    }
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return std::tolower(c); });
    const auto* found = std::ranges::find(kAudioFormats, name, &std::pair<std::string_view, AudioFormat>::first);
    if (found == kAudioFormats.end()) {
        throw AppError(
            "give a format of wav, flac, ogg, opus or mp3, by the format param or the path's suffix, not \"" + name +
            "\"");
    }
    AudioEncoding encoding{.format = found->second};
    if (quality && (std::isnan(*quality) || *quality < 0 || *quality > 1)) {
        throw AppError("give quality from 0 to 1");
    }
    encoding.quality = quality.value_or(encoding.quality);
    return encoding;
}

namespace {

int sndFileFormat(AudioFormat format) {
    switch (format) {
        case AudioFormat::Wav:
            return SF_FORMAT_WAV | SF_FORMAT_FLOAT;
        case AudioFormat::Flac:
            return SF_FORMAT_FLAC | SF_FORMAT_PCM_24;
        case AudioFormat::Ogg:
            return SF_FORMAT_OGG | SF_FORMAT_VORBIS;
        case AudioFormat::Opus:
            return SF_FORMAT_OGG | SF_FORMAT_OPUS;
        case AudioFormat::Mp3:
            return SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III;
    }
    return 0;
}

void setEncoderQuality(SNDFILE* file, AudioEncoding encoding) {
    if (encoding.format == AudioFormat::Flac) {
        int clip = SF_TRUE;
        sf_command(file, SFC_SET_CLIPPING, &clip, sizeof(clip));
    }
    if (encoding.format == AudioFormat::Wav || encoding.format == AudioFormat::Flac) {
        return;
    }
    if (encoding.format == AudioFormat::Mp3) {
        int mode = SF_BITRATE_MODE_VARIABLE;
        sf_command(file, SFC_SET_BITRATE_MODE, &mode, sizeof(mode));
    }
    double compression = 1 - encoding.quality;
    sf_command(file, SFC_SET_COMPRESSION_LEVEL, &compression, sizeof(compression));
}

}  // namespace

struct AudioWriter::File {
    std::filesystem::path path;
    SndFile file;
};

AudioWriter::AudioWriter(const std::filesystem::path& path, std::size_t channels, double sampleRate,
                         AudioEncoding encoding) {
    SF_INFO info{.frames = 0,
                 .samplerate = static_cast<int>(std::lround(sampleRate)),
                 .channels = static_cast<int>(channels),
                 .format = sndFileFormat(encoding.format),
                 .sections = 0,
                 .seekable = 0};
    if (sf_format_check(&info) == SF_FALSE) {
        throw AppError(std::format("this build's libsndfile has no {} encoder for {} channels at {} Hz",
                                   audioFormatName(encoding.format), info.channels, info.samplerate));
    }
    file_ = std::make_unique<File>(File{.path = path, .file = SndFile{sf_open(path.c_str(), SFM_WRITE, &info)}});
    if (!file_->file) {
        throw AppError("cannot write " + path.string() + ": " + sf_strerror(nullptr));
    }
    setEncoderQuality(file_->file.get(), encoding);
}

AudioWriter::~AudioWriter() = default;
AudioWriter::AudioWriter(AudioWriter&&) noexcept = default;
AudioWriter& AudioWriter::operator=(AudioWriter&&) noexcept = default;

void AudioWriter::write(std::span<const std::vector<float>> channels) {
    const std::size_t frames = channels.empty() ? 0 : channels.front().size();
    std::vector<float> interleaved(frames * channels.size());
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            interleaved[frame * channels.size() + channel] = channels[channel][frame];
        }
    }
    // The Vorbis encoder reserves stack in proportion to the frames of one call, so we hand it a block at a time.
    constexpr std::size_t kBlockFrames = 4096;
    for (std::size_t first = 0; first < frames; first += kBlockFrames) {
        const std::size_t count = std::min(kBlockFrames, frames - first);
        if (sf_writef_float(file_->file.get(), interleaved.data() + (first * channels.size()),
                            static_cast<sf_count_t>(count)) != static_cast<sf_count_t>(count)) {
            throw AppError("cannot write " + file_->path.string());
        }
    }
}

void writeWav(const std::filesystem::path& path, const std::vector<std::vector<float>>& channels, double sampleRate) {
    writeAudio(path, channels, sampleRate, {});
}

void writeAudio(const std::filesystem::path& path, const std::vector<std::vector<float>>& channels, double sampleRate,
                AudioEncoding encoding) {
    AudioWriter(path, channels.size(), sampleRate, encoding).write(channels);
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
