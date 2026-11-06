#pragma once

#include <readerwritercircularbuffer.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/engine/audio_backend.hpp"
#include "snapshot.hpp"
#include "spsc_ring.hpp"

namespace emerick::engine {

class RenderPipeline;

inline constexpr std::size_t kCommandCapacity = 1024;
inline constexpr std::size_t kRetiredCapacity = 64;
inline constexpr std::size_t kRetiredPreviewCapacity = 8;
inline constexpr std::size_t kMaxTrackIdLength = 64;
inline constexpr std::size_t kMetronomeVoices = 16;
inline constexpr std::size_t kOutgoingMidiBytes = std::size_t{1} << 18;

// A message the UI thread sends to an output port or a track's instrument, followed by the port or track id and the
// message bytes.
struct OutgoingMidiHeader {
    std::uint32_t targetIdSize = 0;
    std::uint32_t size = 0;
    bool toInstrument = false;
};

struct Command {
    enum class Type : std::uint8_t { play, stop, locate, record, mute, solo, arm, gain, pan, panic, previewMetronome };
    Type type = Type::play;
    std::array<char, kMaxTrackIdLength> trackId{};
    std::size_t trackIdLength = 0;
    Ticks ticks = 0;
    float gain = 1.0F;
    float pan = 0.0F;
    bool on = false;

    [[nodiscard]] std::string_view track() const { return {trackId.data(), trackIdLength}; }
};

// A metronome sound that is playing; position counts frames from its start and is negative before it.
struct MetronomeVoice {
    DrumVoice voice = DrumVoice::beat;
    Frames position = 0;
    float gain = 0.0F;
    bool active = false;
};

// A rendered sound that plays once into a track's mix, before its inserts and fader.
struct PreviewSound {
    std::string trackId;
    std::vector<std::vector<float>> channels;
    Frames position = 0;
};

struct RealtimeState {
    std::atomic<Snapshot*> pending{nullptr};
    moodycamel::BlockingReaderWriterCircularBuffer<Snapshot*> retired{kRetiredCapacity};
    moodycamel::BlockingReaderWriterCircularBuffer<Command> commands{kCommandCapacity};
    std::atomic<Frames> publishedFrame{0};
    std::atomic<bool> publishedPlaying{false};
    std::atomic<std::uint64_t> locatesApplied{0};
    // The UI thread writes messages here before the command that should follow them.
    SpscRing outgoingMidi{kOutgoingMidiBytes};
    std::atomic<PreviewSound*> pendingPreview{nullptr};
    moodycamel::BlockingReaderWriterCircularBuffer<PreviewSound*> retiredPreviews{kRetiredPreviewCapacity};
    // Set before the audio thread takes a pending preview and cleared once it retires it, so the UI thread never
    // closes the stream under a preview.
    std::atomic<bool> previewSounding{false};
    // Set by the UI thread when it asks for a metronome preview and cleared once the preview ends.
    std::atomic<bool> metronomePreviewing{false};

    // The audio thread owns everything below while the stream is open.
    Snapshot* current = nullptr;
    Frames frame = 0;
    bool transportPlaying = false;
    // We send Start after the events at the start frame, so a keyboard has its Style section and chord before it
    // starts.
    bool startPending = false;
    bool recordEnabled = false;
    bool taking = false;
    Frames takeStartFrame = 0;
    Frames countInLeft = 0;
    Frames metronomePreviewLeft = 0;
    // Counts frames while stopped, so the clock we send then keeps its pulse spacing.
    Frames stoppedClockFrame = 0;
    std::array<MetronomeVoice, kMetronomeVoices> metronomeVoices{};
    const DrumSounds* voiceSounds = nullptr;
    PreviewSound* preview = nullptr;
    // Set only for an offline render, which mixes each block's buses on a thread of their own.
    RenderPipeline* pipeline = nullptr;
};

void processBlock(RealtimeState& state, const AudioBlock& block);

}  // namespace emerick::engine
