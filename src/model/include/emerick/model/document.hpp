#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "emerick/core/fade.hpp"
#include "emerick/core/id.hpp"
#include "emerick/core/time.hpp"

namespace emerick::model {

using core::Ticks;

struct TrackTag {};
struct ClipTag {};
struct MarkerTag {};
using TrackId = core::Id<TrackTag>;
using ClipId = core::Id<ClipTag>;
using MarkerId = core::Id<MarkerTag>;

// Channels are 1-based.
struct AudioRoute {
    std::string deviceId;
    std::vector<int> channels;

    bool operator==(const AudioRoute&) const = default;
};

// Channel 0 is all channels, 1 to 16 one channel. The track with sysex set claims sysex from the port.
struct MidiRoute {
    std::string portId;
    int channel = 0;
    bool thru = false;
    bool sysex = false;

    bool operator==(const MidiRoute&) const = default;
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
    core::FadeShape fadeInShape = core::FadeShape::linear;
    core::FadeShape fadeOutShape = core::FadeShape::linear;
    double fadeInCurve = 0.0;
    double fadeOutCurve = 0.0;

    bool operator==(const AudioClip&) const = default;
};

struct MidiEvent {
    Ticks at = 0;  // relative to the clip start
    std::vector<std::uint8_t> bytes;

    bool operator==(const MidiEvent&) const = default;
};

struct MidiClip {
    ClipId id = ClipId::make();
    Ticks start = 0;
    Ticks length = 0;
    std::vector<MidiEvent> events;

    bool operator==(const MidiClip&) const = default;
};

// Clips may overlap; overlapping clips stack as takes and nothing is overwritten. A later clip in the list plays over
// earlier ones wherever it plays at full gain, so its fades crossfade with the clips beneath it.
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

    bool operator==(const Track&) const = default;
};

// A named position on the timeline, as on Logic's marker track. The color indexes the track colors; a marker without
// one shows in the lane's own color.
struct Marker {
    MarkerId id = MarkerId::make();
    std::string name;
    Ticks at = 0;
    std::optional<int> color;

    bool operator==(const Marker&) const = default;
};

struct Document {
    core::TempoMap tempo;
    std::vector<Track> tracks;
    std::vector<Marker> markers;                           // sorted by position
    std::vector<std::string> clockPorts;                   // MIDI outputs that get clock, Start and Stop
    nlohmann::json pluginData = nlohmann::json::object();  // one plugin-owned value per plugin id

    bool operator==(const Document&) const = default;
};

struct ClipRef {
    const Track* track;
    const Clip* clip;
};

const ClipId& clipId(const Clip& clip);

Track* findTrack(Document& document, const TrackId& id);
const Track* findTrack(const Document& document, const TrackId& id);
std::optional<ClipRef> findClip(const Document& document, const ClipId& id);
const Marker* findMarker(const Document& document, const MarkerId& id);

}  // namespace emerick::model
