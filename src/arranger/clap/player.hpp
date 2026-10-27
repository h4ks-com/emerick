#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "emerick/arranger/arranger.hpp"
#include "emerick/core/time.hpp"
#include "emerick/yamaha/chord.hpp"
#include "emerick/yamaha/sysex.hpp"

namespace emerick::arranger {

// Plays an arranger on the chords and sections a host sends, one process block at a time, following the host's
// transport: it starts with the transport, stops with it, and starts over from the last section and chord it was sent
// when playback jumps, which the host's chase sends right at the jump. Audio thread only; it never allocates.
class Player {
public:
    struct Output {
        std::uint32_t frame = 0;
        std::array<std::uint8_t, 3> bytes{};
    };

    // The host transport at the block's first frame, in core ticks.
    struct Block {
        std::uint32_t frames = 0;
        bool playing = false;
        double ticks = 0.0;
        double ticksPerFrame = 0.0;
        core::Ticks ticksPerBar = core::kTicksPerQuarter * 4;
        bool looping = false;
        double loopStart = 0.0;
        double loopEnd = 0.0;
    };

    Player();

    // A new arranger, or null without a style, sends its voices and takes over from the last one.
    void begin(const Block& block, Arranger* arranger, bool autoFill);
    void chord(std::uint32_t frame, const yamaha::Chord& chord);
    void section(std::uint32_t frame, const yamaha::SectionPress& press);
    // The Main the last section asked for, which Fill Up and Fill Down move from.
    [[nodiscard]] std::optional<int> playingMain() const { return playingMain_; }
    // Everything the block sends, in frame order; valid until the next begin.
    std::span<const Output> finish();

private:
    struct Request {
        yamaha::Section kind = yamaha::Section::Main;
        int variation = 0;
    };

    [[nodiscard]] core::Ticks tickAt(std::uint32_t frame) const;
    [[nodiscard]] bool live() const;
    void remember(const yamaha::SectionPress& press);
    void reach(std::uint32_t frame);
    void renderTo(std::uint32_t frame);
    void startAt(core::Ticks at);
    void stopAt(core::Ticks at, std::uint32_t frame);
    void emit(std::span<const MidiEvent> events, std::optional<std::uint32_t> frame);

    Arranger* arranger_ = nullptr;
    Block block_;
    bool wasPlaying_ = false;
    double expected_ = 0.0;
    double baseTicks_ = 0.0;
    std::uint32_t baseFrame_ = 0;
    std::optional<std::uint32_t> jumpFrame_;
    std::optional<std::uint32_t> restartFrame_;
    std::optional<core::Ticks> deferredStart_;
    core::Ticks cursor_ = 0;
    std::optional<Request> target_;
    std::optional<yamaha::Chord> targetChord_;
    std::optional<int> playingMain_;
    std::vector<Output> out_;
    std::uint32_t lastFrame_ = 0;
};

}  // namespace emerick::arranger
