#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "emerick/core/time.hpp"

namespace emerick::engine {

using core::Frames;
using core::Ticks;

enum class TrackKind : std::uint8_t { audio, midi };

struct AudioRoute {
    std::string deviceId;
    std::vector<int> channels;  // 1-based device channels
};

struct MidiRoute {
    std::string portId;
    int channel = 0;  // 1 to 16, 0 means all
};

struct AudioClip {
    std::filesystem::path path;
    Ticks start = 0;
    Ticks length = 0;
    Frames sourceOffset = 0;
    float gain = 1.0F;
    Ticks fadeIn = 0;
    Ticks fadeOut = 0;
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

struct Track {
    std::string id;
    TrackKind kind = TrackKind::audio;
    bool armed = false;
    bool mute = false;
    bool solo = false;
    bool monitor = false;
    float gain = 1.0F;
    AudioRoute audioIn;
    AudioRoute audioOut;
    MidiRoute midiIn;
    MidiRoute midiOut;
    bool midiThru = false;
    bool claimsSysex = false;
    std::vector<AudioClip> audioClips;
    std::vector<MidiClip> midiClips;
};

struct Session {
    core::TempoMap tempo;
    std::vector<Track> tracks;
    std::vector<std::string> clockPorts;  // MIDI outputs that get clock, Start and Stop
    std::filesystem::path takeFolder;     // recorded audio takes are written here
};

// A clip we skipped because its file is missing or at another sample rate.
struct SessionError {
    std::string trackId;
    std::string message;
};

struct RecordedTake {
    std::string trackId;
    Ticks startTick = 0;
    std::filesystem::path audioFile;    // set for audio tracks whose file we could write
    std::vector<MidiEvent> midiEvents;  // set for MIDI tracks, ticks relative to startTick
    std::string error;                  // set when part or all of the take was lost
};

}  // namespace emerick::engine
