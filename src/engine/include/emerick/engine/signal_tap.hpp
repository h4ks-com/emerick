#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace emerick::engine {

// One thread writes and one other thread reads; a write that finds the ring full drops what does not fit.
class SampleRing {
public:
    explicit SampleRing(std::size_t capacity) : data_(std::bit_ceil(capacity)), mask_(data_.size() - 1) {}

    [[nodiscard]] std::size_t capacity() const { return data_.size(); }
    [[nodiscard]] std::size_t readable() const {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_relaxed);
    }

    // Writes the mean of the channels, a range of sample pointers, for each of the frames, and returns how many frames
    // fit.
    template <class Channels>
    std::size_t writeMean(const Channels& channels, std::size_t frames) {
        if (std::ranges::empty(channels)) {
            return 0;
        }
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t space = data_.size() - (head - tail_.load(std::memory_order_acquire));
        const std::size_t count = frames < space ? frames : space;
        const float scale = 1.0F / static_cast<float>(std::ranges::size(channels));
        for (std::size_t frame = 0; frame < count; ++frame) {
            float sum = 0.0F;
            for (const float* channel : channels) {
                sum += channel[frame];
            }
            data_[(head + frame) & mask_] = sum * scale;
        }
        head_.store(head + count, std::memory_order_release);
        return count;
    }

    std::size_t write(std::span<const float> samples) {
        const std::array channels{samples.data()};
        return writeMean(channels, samples.size());
    }

    // Reads up to out.size() samples and returns how many it read.
    std::size_t read(std::span<float> out) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t count = std::min(out.size(), head_.load(std::memory_order_acquire) - tail);
        for (std::size_t index = 0; index < count; ++index) {
            out[index] = data_[(tail + index) & mask_];
        }
        tail_.store(tail + count, std::memory_order_release);
        return count;
    }

private:
    std::vector<float> data_;
    std::size_t mask_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

// Input taps an audio track's live input, output a track's or bus's signal after its fader and pan.
enum class TapPoint : std::uint8_t { input, output };

// The audio thread writes the mono mix of the tapped signal into ring each block, and an analysis thread reads it. An
// empty track id taps the master: the stream's first two outputs after every track has played into them.
struct SignalTap {
    static constexpr std::size_t kRingSamples = std::size_t{1} << 17;

    std::string trackId;
    TapPoint point = TapPoint::output;
    SampleRing ring{kRingSamples};
};

}  // namespace emerick::engine
