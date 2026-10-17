#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "emerick/core/id.hpp"
#include "emerick/core/time.hpp"

namespace emerick::model {

using core::Ticks;

struct TrackTag {};
struct ClipTag {};
using TrackId = core::Id<TrackTag>;
using ClipId = core::Id<ClipTag>;

// Channels are 1-based.
struct AudioRoute {
    std::string deviceId;
    std::vector<int> channels;
};

// Channel 0 is all channels, 1 to 16 one channel. The track with sysex set claims sysex from the port.
struct MidiRoute {
    std::string portId;
    int channel = 0;
    bool thru = false;
    bool sysex = false;
};

struct AudioClip {
    ClipId id = ClipId::make();
    Ticks start = 0;
    Ticks length = 0;
    core::Frames sourceOffset = 0;
    std::string file;
    double gainDb = 0.0;
    Ticks fadeIn = 0;
    Ticks fadeOut = 0;
};

struct MidiEvent {
    Ticks at = 0;  // relative to the clip start
    std::vector<std::uint8_t> bytes;
};

struct MidiClip {
    ClipId id = ClipId::make();
    Ticks start = 0;
    Ticks length = 0;
    std::vector<MidiEvent> events;
};

// Clips may overlap; overlapping clips stack as takes and nothing is overwritten.
using Clip = std::variant<AudioClip, MidiClip>;

struct Track {
    TrackId id = TrackId::make();
    std::string name;
    std::string kind = "audio";  // "audio", "midi" or a plugin kind such as "yamaha.chord"
    std::string icon;
    int color = 0;
    bool mute = false;
    bool solo = false;
    bool armed = false;
    bool monitor = false;
    double gainDb = 0.0;
    AudioRoute audioIn;
    AudioRoute audioOut;
    MidiRoute midiIn;
    MidiRoute midiOut;
    std::vector<Clip> clips;
    nlohmann::json data;  // plugin-owned, the model never interprets it
};

struct Document {
    core::TempoMap tempo;
    std::vector<Track> tracks;
    std::vector<std::string> clockPorts;                   // MIDI outputs that get clock, Start and Stop
    nlohmann::json pluginData = nlohmann::json::object();  // one plugin-owned value per plugin id
};

struct ClipRef {
    const Track* track;
    const Clip* clip;
};

const ClipId& clipId(const Clip& clip);

Track* findTrack(Document& document, const TrackId& id);
const Track* findTrack(const Document& document, const TrackId& id);
std::optional<ClipRef> findClip(const Document& document, const ClipId& id);

}  // namespace emerick::model
