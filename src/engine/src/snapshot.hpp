#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "clip_stream.hpp"
#include "emerick/engine/engine.hpp"
#include "emerick/engine/session.hpp"
#include "metronome_sounds.hpp"
#include "midi_ports.hpp"
#include "take_recorder.hpp"

namespace emerick::engine {

inline constexpr std::size_t kMaxIncomingMessage = std::size_t{1} << 16;
inline constexpr std::size_t kMidiChannels = 16;
inline constexpr std::size_t kMidiNotes = 128;

struct ClipPlayback {
    std::shared_ptr<ClipStream> stream;
    Frames start = 0;
    Frames length = 0;
    float gain = 1.0F;
    Frames fadeIn = 0;
    Frames fadeOut = 0;
    core::FadeCurve fadeInCurve;
    core::FadeCurve fadeOutCurve;
};

struct ScheduledMidi {
    Frames frame = 0;
    std::size_t offset = 0;
    std::size_t size = 0;
};

// The audio thread raises the peaks and the clip flag, and the UI thread reads and resets them.
struct LevelMeter {
    std::array<std::atomic<float>, kMeterChannels> peaks{};
    std::atomic<bool> clipped{false};
    // Only the UI thread touches this.
    std::size_t channels = 0;
};

struct TrackState {
    std::string id;
    TrackKind kind = TrackKind::audio;
    bool armed = false;
    bool mute = false;
    bool solo = false;
    bool monitor = false;
    float gain = 1.0F;
    float pan = 0.0F;
    std::vector<int> inputs;
    std::vector<int> outputs;
    std::vector<const float*> inputPointers;
    std::shared_ptr<MidiInputPort> midiIn;
    int inChannel = 0;
    std::shared_ptr<MidiOutputPort> midiOut;
    int outChannel = 0;
    bool thru = false;
    bool claimsSysex = false;
    std::vector<ClipPlayback> clips;
    std::vector<ScheduledMidi> midiEvents;
    std::vector<std::uint8_t> midiBytes;
    std::array<std::bitset<kMidiNotes>, kMidiChannels> soundingNotes{};
    std::shared_ptr<TakeRecorder> recorder;
    std::shared_ptr<LevelMeter> inputMeter;
    std::shared_ptr<LevelMeter> outputMeter;
    // The track's signal per output for the current block, so we apply its fader and pan and meter it before it joins
    // the mix.
    std::vector<std::vector<float>> mix;
    // The gain per output that the last block ended on, so we ramp from it and a fader move never clicks.
    std::vector<float> outputGains;
};

struct MetronomeState {
    Metronome settings;
    std::vector<int> outputs;
    std::shared_ptr<const DrumSounds> sounds;
    std::shared_ptr<MidiOutputPort> midiOut;
    Frames countInFrames = 0;
};

// The UI thread builds a snapshot and hands it over whole; from then on only the audio thread touches it.
struct Snapshot {
    core::TempoMap tempo;
    double sampleRate = 48000.0;
    PanLaw panLaw = PanLaw::compensated;
    std::vector<TrackState> tracks;
    std::vector<std::shared_ptr<MidiOutputPort>> clockPorts;
    std::vector<std::shared_ptr<MidiInputPort>> inputs;
    std::vector<std::uint8_t> incoming = std::vector<std::uint8_t>(kMaxIncomingMessage);
    MetronomeState metronome;
    // Output pointers for the part of a block after a count-in ends, one per stream output.
    std::vector<float*> tailOutputs;
};

inline bool anySolo(const Snapshot& snapshot) { return std::ranges::any_of(snapshot.tracks, &TrackState::solo); }

// Pan applies to a stereo output pair; a track on one output or on more than two plays at its gain on each.
inline float outputGain(const TrackState& track, std::size_t output, PanLaw law) {
    if (track.outputs.size() != 2) {
        return track.gain;
    }
    const StereoGains pan = panGains(track.pan, law);
    return track.gain * (output == 0 ? pan.left : pan.right);
}

inline bool audible(const TrackState& track, bool soloActive) { return !track.mute && (!soloActive || track.solo); }

}  // namespace emerick::engine
