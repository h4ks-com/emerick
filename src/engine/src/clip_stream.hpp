#pragma once

#include <sndfile.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "emerick/core/time.hpp"
#include "emerick/engine/sound_file.hpp"
#include "spsc_ring.hpp"

namespace emerick::engine {

using core::Frames;

struct ClipFile {
    SndFile file;
    SF_INFO info{};
};

std::expected<ClipFile, std::string> openClipFile(const std::filesystem::path& path, double sampleRate);

// Audio of a clip from start on, read while building a snapshot. A cycle jump plays it while the stream reads ahead
// from its end.
struct Preload {
    Frames start = 0;
    Frames frames = 0;
    std::vector<float> samples;
};

// Reads up to frames frames of the clip from local on, or nullopt when the file cannot be read.
std::optional<Preload> preloadClip(const std::filesystem::path& path, double sampleRate, Frames sourceOffset,
                                   Frames local, Frames frames);

// We call read and seek on the audio thread and poll on the reader thread, which fills the ring ahead of the playhead.
class ClipStream {
public:
    ClipStream(ClipFile opened, Frames sourceOffset, Frames length, double sampleRate, Frames maxBlock);

    [[nodiscard]] int channels() const { return channels_; }
    // True once the reader filled the ring after the last seek and after the last read that took data from it.
    [[nodiscard]] bool buffered() const;

    // Data that arrives late reads as silence. Frames in preload come from it while the stream waits at its end, as
    // after a cycle jump.
    std::span<const float> read(Frames local, Frames count, const Preload* preload = nullptr);
    void seek(Frames local);

    void poll();

private:
    struct Header {
        std::uint32_t generation = 0;
        std::uint32_t frames = 0;
        Frames start = 0;
    };

    [[nodiscard]] std::size_t samplesFor(Frames frames) const;
    [[nodiscard]] std::size_t bytesFor(Frames frames) const;

    SndFile file_;
    int channels_ = 0;
    Frames sourceOffset_ = 0;
    Frames length_ = 0;
    SpscRing ring_;
    std::atomic<std::uint32_t> generation_{0};
    std::atomic<Frames> seekTarget_{0};
    std::atomic<std::uint32_t> bufferedGeneration_{std::numeric_limits<std::uint32_t>::max()};
    std::atomic<bool> filled_{false};

    std::uint32_t audioGeneration_ = 0;
    Frames expected_ = 0;
    Frames recordAt_ = 0;
    Frames remaining_ = 0;
    std::vector<float> scratch_;

    std::uint32_t readerGeneration_ = 0;
    Frames next_ = 0;
    std::vector<float> readBuffer_;
};

}  // namespace emerick::engine
