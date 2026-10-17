#pragma once

#include <readerwritercircularbuffer.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "emerick/engine/audio_backend.hpp"
#include "snapshot.hpp"

namespace emerick::engine {

inline constexpr std::size_t kCommandCapacity = 1024;
inline constexpr std::size_t kRetiredCapacity = 64;
inline constexpr std::size_t kMaxTrackIdLength = 64;

struct Command {
    enum class Type : std::uint8_t { play, stop, locate, record, mute, solo, arm, gain };
    Type type = Type::play;
    std::array<char, kMaxTrackIdLength> trackId{};
    std::size_t trackIdLength = 0;
    Ticks ticks = 0;
    float gain = 1.0F;
    bool on = false;

    [[nodiscard]] std::string_view track() const { return {trackId.data(), trackIdLength}; }
};

struct RealtimeState {
    std::atomic<Snapshot*> pending{nullptr};
    moodycamel::BlockingReaderWriterCircularBuffer<Snapshot*> retired{kRetiredCapacity};
    moodycamel::BlockingReaderWriterCircularBuffer<Command> commands{kCommandCapacity};
    std::atomic<Frames> publishedFrame{0};
    std::atomic<bool> publishedPlaying{false};
    std::atomic<std::uint64_t> locatesApplied{0};

    // The audio thread owns everything below while the stream is open.
    Snapshot* current = nullptr;
    Frames frame = 0;
    bool transportPlaying = false;
    bool recordEnabled = false;
    bool taking = false;
    Frames takeStartFrame = 0;
};

void processBlock(RealtimeState& state, const AudioBlock& block);

}  // namespace emerick::engine
