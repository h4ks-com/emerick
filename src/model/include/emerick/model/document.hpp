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
struct NoteTag {};
using TrackId = core::Id<TrackTag>;
using ClipId = core::Id<ClipTag>;
using MarkerId = core::Id<MarkerTag>;
using NoteId = core::Id<NoteTag>;

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

// A note's channel counts from 1, as on the track routes.
struct Note {
    NoteId id = NoteId::make();
    int pitch = 60;
    int velocity = 100;
    Ticks start = 0;  // relative to the clip start
    Ticks length = 0;
    int channel = 1;

    bool operator==(const Note&) const = default;
};

// Notes hold every note-on with its note-off, and events every other message, so each message has one home.
struct MidiClip {
    ClipId id = ClipId::make();
    Ticks start = 0;
    Ticks length = 0;
    std::vector<MidiEvent> events;
    std::vector<Note> notes;

    bool operator==(const MidiClip&) const = default;
};

// Clips may overlap; overlapping clips stack as takes and nothing is overwritten. A later clip in the list plays over
// earlier ones wherever it plays at full gain, so its fades crossfade with the clips beneath it.
using Clip = std::variant<AudioClip, MidiClip>;

// A send feeds a copy of a track's signal to a bus, before or after the track's fader and pan.
struct Send {
    TrackId bus;
    double levelDb = 0.0;
    bool pre = false;
    bool enabled = true;

    bool operator==(const Send&) const = default;
};

struct Track {
    TrackId id = TrackId::make();
    std::string name;
    std::string kind = "audio";  // "audio", "midi", "bus" or a plugin kind such as "yamaha.chord"
    std::string icon;
    int color = 0;
    bool mute = false;
    bool solo = false;
    bool armed = false;
    bool monitor = false;
    double gainDb = 0.0;
    double pan = 0.0;  // -1 hard left, 0 center, 1 hard right
    AudioRoute audioIn;
    AudioRoute audioOut;
    MidiRoute midiIn;
    MidiRoute midiOut;
    // A track with an output bus plays into that bus in place of its audio output.
    TrackId outputBus;
    std::vector<Send> sends;
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
// The buses a track plays into through its output bus and its sends. Disabled sends count too, so enabling one never
// closes a loop.
std::vector<TrackId> busesFedBy(const Track& track);
// True when audio from track from arrives at track to through sends and output buses.
bool reaches(const Document& document, const TrackId& from, const TrackId& to);

}  // namespace emerick::model
