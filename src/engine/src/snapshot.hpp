#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "clip_stream.hpp"
#include "emerick/engine/session.hpp"
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
};

struct ScheduledMidi {
    Frames frame = 0;
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct TrackState {
    std::string id;
    TrackKind kind = TrackKind::audio;
    bool armed = false;
    bool mute = false;
    bool solo = false;
    bool monitor = false;
    float gain = 1.0F;
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
};

// The UI thread builds a snapshot and hands it over whole; from then on only the audio thread touches it.
struct Snapshot {
    core::TempoMap tempo;
    double sampleRate = 48000.0;
    std::vector<TrackState> tracks;
    std::vector<std::shared_ptr<MidiOutputPort>> clockPorts;
    std::vector<std::shared_ptr<MidiInputPort>> inputs;
    std::vector<std::uint8_t> incoming = std::vector<std::uint8_t>(kMaxIncomingMessage);
};

inline bool anySolo(const Snapshot& snapshot) { return std::ranges::any_of(snapshot.tracks, &TrackState::solo); }

inline bool audible(const TrackState& track, bool soloActive) { return !track.mute && (!soloActive || track.solo); }

}  // namespace emerick::engine
