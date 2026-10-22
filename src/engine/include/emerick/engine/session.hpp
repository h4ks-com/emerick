#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "emerick/core/fade.hpp"
#include "emerick/core/time.hpp"

namespace emerick::engine {

using core::Frames;
using core::Ticks;

// A bus plays the sum of the sends and tracks routed into it.
enum class TrackKind : std::uint8_t { audio, midi, bus };

struct AudioRoute {
    std::string deviceId;
    std::vector<int> channels;  // 1-based device channels
    bool operator==(const AudioRoute&) const = default;
};

struct MidiRoute {
    std::string portId;
    int channel = 0;  // 1 to 16, 0 means all
    bool operator==(const MidiRoute&) const = default;
};

struct AudioClip {
    std::filesystem::path path;
    Ticks start = 0;
    Ticks length = 0;
    Frames sourceOffset = 0;
    float gain = 1.0F;
    Ticks fadeIn = 0;
    Ticks fadeOut = 0;
    core::FadeCurve fadeInCurve;
    core::FadeCurve fadeOutCurve;
};

struct MidiEvent {
    Ticks tick = 0;
    std::vector<std::uint8_t> bytes;
    auto operator<=>(const MidiEvent&) const = default;
};

struct MidiClip {
    Ticks start = 0;
    std::vector<MidiEvent> events;  // ticks relative to start
};

// A pre-fader send taps the track before its fader and pan, a post-fader send after them.
struct Send {
    std::string bus;
    float gain = 1.0F;
    bool pre = false;
    bool operator==(const Send&) const = default;
};

struct Track {
    std::string id;
    TrackKind kind = TrackKind::audio;
    bool armed = false;
    bool mute = false;
    bool solo = false;
    bool monitor = false;
    float gain = 1.0F;
    float pan = 0.0F;  // -1 hard left, 0 center, 1 hard right
    AudioRoute audioIn;
    AudioRoute audioOut;
    MidiRoute midiIn;
    MidiRoute midiOut;
    bool midiThru = false;
    bool claimsSysex = false;
    // A track with an output bus plays into that bus in place of audioOut.
    std::string outputBus;
    std::vector<Send> sends;
    std::vector<AudioClip> audioClips;
    std::vector<MidiClip> midiClips;
};

// Click and sample play one sound per beat, a groove plays a drum pattern, and the MIDI modes send either as notes.
enum class MetronomeMode : std::uint8_t { click, sample, groove, midiClick, midiGroove };
enum class GroovePattern : std::uint8_t { rock, fourOnTheFloor, halfTime, shuffle, bossa };
enum class DrumVoice : std::uint8_t { accent, beat, kick, snare, hat };
inline constexpr std::size_t kDrumVoices = 5;
inline constexpr int kMaxCountInBars = 4;
// The id a SessionError about the metronome names in place of a track id.
inline constexpr const char* kMetronomeId = "metronome";

struct Metronome {
    bool enabled = false;
    bool duringPlayback = false;
    bool duringRecording = true;
    // Bars of click before a recording started from stop; the take starts after them.
    int countInBars = 0;
    MetronomeMode mode = MetronomeMode::click;
    GroovePattern pattern = GroovePattern::rock;
    float gain = 0.5F;
    float accentGain = 2.0F;
    std::vector<int> accentBeats = {1};  // 1-based beats of the bar
    AudioRoute audioOut;                 // an empty device plays on the stream's first two outputs
    std::filesystem::path accentSample;
    std::filesystem::path beatSample;
    MidiRoute midiOut = {.portId = {}, .channel = 10};
    std::array<std::uint8_t, kDrumVoices> notes = {76, 77, 36, 38, 42};  // GM wood blocks, kick, snare, closed hat
    bool operator==(const Metronome&) const = default;
};

// How loud a centered track plays on each side of a stereo output pair. Compensated keeps a centered track at full
// level and raises a hard-panned one by 3 dB on its side; every other law plays a hard-panned track at full level.
enum class PanLaw : std::uint8_t { compensated, zeroDb, constantPower, compromise, linear };

struct Session {
    core::TempoMap tempo;
    std::vector<Track> tracks;
    std::vector<std::string> clockPorts;  // MIDI outputs that get clock, Start and Stop
    std::filesystem::path takeFolder;     // recorded audio takes are written here
    Metronome metronome;
    PanLaw panLaw = PanLaw::compensated;
};

// A clip we skipped because its file is missing or at another sample rate.
struct SessionError {
    std::string trackId;
    std::string message;
};

// A float sample at this magnitude or above was clipped before or at the converter.
inline constexpr float kFullScale = 1.0F;

struct RecordedTake {
    std::string trackId;
    Ticks startTick = 0;
    std::filesystem::path audioFile;    // set for audio tracks whose file we could write
    std::vector<MidiEvent> midiEvents;  // set for MIDI tracks, ticks relative to startTick
    std::string error;                  // set when part or all of the take was lost
    std::uint64_t samples = 0;          // audio samples written, every channel counted
    std::uint64_t clippedSamples = 0;   // of those, the ones at full scale
    // NaN or infinite input samples, written as zeros.
    std::uint64_t nonFiniteSamples = 0;
    // Frames the writer fell behind on, written as silence so later audio stays in time.
    Frames droppedFrames = 0;
    std::uint64_t droppedMidiEvents = 0;
};

}  // namespace emerick::engine
