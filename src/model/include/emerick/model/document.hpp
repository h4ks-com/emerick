#pragma once

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "emerick/core/automation.hpp"
#include "emerick/core/fade.hpp"
#include "emerick/core/id.hpp"
#include "emerick/core/plugin_format.hpp"
#include "emerick/core/time.hpp"

namespace emerick::model {

using core::Ticks;

struct TrackTag {};
struct ClipTag {};
struct MarkerTag {};
struct NoteTag {};
struct InsertTag {};
struct AutomationPointTag {};
using TrackId = core::Id<TrackTag>;
using ClipId = core::Id<ClipTag>;
using MarkerId = core::Id<MarkerTag>;
using NoteId = core::Id<NoteTag>;
using InsertId = core::Id<InsertTag>;
using AutomationPointId = core::Id<AutomationPointTag>;

// Channels are 1-based.
struct AudioRoute {
    std::string deviceId;
    std::vector<int> channels;

    bool operator==(const AudioRoute&) const = default;
};

// Channel 0 is all channels, 1 to 16 one channel. The track with sysex set claims sysex from the port. An input with a
// source track takes the MIDI that track's instrument sends in place of a port's.
struct MidiRoute {
    std::string portId;
    int channel = 0;
    bool thru = false;
    bool sysex = false;
    TrackId sourceTrack;

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
    bool muted = false;
    std::string name;

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
    bool muted = false;
    // How far the clip runs on the timeline repeating its content, as a looped region in Logic. The content keeps its
    // own length, and the clip loops only while loopLength is longer.
    Ticks loopLength = 0;
    std::string name;

    bool operator==(const MidiClip&) const = default;
};

// Clips may overlap; overlapping clips stack as takes and nothing is overwritten. A later clip in the list plays over
// earlier ones wherever it plays at full gain, so its fades crossfade with the clips beneath it. A muted clip plays
// nothing, so the clips beneath it play there.
using Clip = std::variant<AudioClip, MidiClip>;

// A send feeds a copy of a track's signal to a bus, before or after the track's fader and pan.
struct Send {
    TrackId bus;
    double levelDb = 0.0;
    bool pre = false;
    bool enabled = true;

    bool operator==(const Send&) const = default;
};

// A CLAP or VST3 plugin, as a track's instrument or an insert. The model keeps the plugin's saved state as bytes it
// never reads, so a project keeps the state while the plugin is missing.
struct PluginSlot {
    std::string pluginId;
    std::string bundle;  // where the plugin was found when it was picked
    std::string name;
    std::vector<std::uint8_t> state;
    core::PluginFormat format = core::PluginFormat::clap;

    bool operator==(const PluginSlot&) const = default;
};

// An effect on a track's signal before its fader. The app reads type and params against its effect table, so a file
// from another build keeps what this one cannot play. A plugin insert's type is kPluginInsertType and its plugin slot
// keeps the plugin with its state, which holds its param values.
struct Insert {
    InsertId id = InsertId::make();
    std::string type;
    bool bypass = false;
    std::map<std::string, double> params;
    std::optional<PluginSlot> plugin = std::nullopt;

    bool operator==(const Insert&) const = default;
};

inline constexpr const char* kPluginInsertType = "plugin";

// The curve shapes the segment from this point to the next.
struct AutomationPoint {
    AutomationPointId id = AutomationPointId::make();
    Ticks tick = 0;
    double value = 0.0;
    core::AutomationCurve curve = core::AutomationCurve::linear;

    bool operator==(const AutomationPoint&) const = default;
};

// A lane automates one parameter, such as "gain", "pan", "mute", "send.<busId>.level" or "<insertId>.<param>". The
// app resolves the parameter id; the model keeps any id. Points stay sorted by tick.
struct AutomationLane {
    std::string param;
    std::vector<AutomationPoint> points;

    bool operator==(const AutomationLane&) const = default;
};

// As in Logic: Off ignores the lanes, Read plays them, Touch writes while a control is held and Latch keeps writing
// its last value until the transport stops.
enum class AutomationMode : std::uint8_t { off, read, touch, latch };

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
    // The level change in dB the track's signal gets before its inserts and fader.
    double trimDb = 0.0;
    double pan = 0.0;  // -1 hard left, 0 center, 1 hard right
    AudioRoute audioIn;
    AudioRoute audioOut;
    MidiRoute midiIn;
    MidiRoute midiOut;
    // A track with an output bus plays into that bus in place of its audio output.
    TrackId outputBus;
    std::vector<Send> sends;
    std::vector<Insert> inserts;  // in signal order
    std::vector<Clip> clips;
    AutomationMode automationMode = AutomationMode::read;
    std::vector<AutomationLane> automation;
    // Moves the track's takes later by this many milliseconds, or earlier when negative, to make up for an
    // instrument whose audio or MIDI reaches us late.
    double recordDelayMs = 0.0;
    nlohmann::json data;  // plugin-owned, the model never interprets it
    std::optional<PluginSlot> instrument;

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

// The region playback repeats while enabled, as Logic's cycle. A region whose end is not after its start is no region.
struct Cycle {
    bool enabled = false;
    Ticks start = 0;
    Ticks end = 0;

    [[nodiscard]] bool active() const { return enabled && end > start; }
    bool operator==(const Cycle&) const = default;
};

struct Document {
    core::TempoMap tempo;
    std::vector<Track> tracks;
    std::vector<Marker> markers;                           // sorted by position
    std::vector<std::string> clockPorts;                   // MIDI outputs that get clock, Start and Stop
    nlohmann::json pluginData = nlohmann::json::object();  // one plugin-owned value per plugin id
    Cycle cycle = {};
    // Saves copy the files the project names into its bundle and keep the loaded style inside project.json.
    bool selfContained = false;

    bool operator==(const Document&) const = default;
};

struct ClipRef {
    const Track* track;
    const Clip* clip;
};

const ClipId& clipId(const Clip& clip);
bool clipMuted(const Clip& clip);
void setClipMuted(Clip& clip, bool muted);
const std::string& clipName(const Clip& clip);
void setClipName(Clip& clip, std::string name);

Track* findTrack(Document& document, const TrackId& id);
const Track* findTrack(const Document& document, const TrackId& id);
std::optional<ClipRef> findClip(const Document& document, const ClipId& id);
// The track whose inserts hold the insert.
const Track* trackWithInsert(const Document& document, const InsertId& id);
const Marker* findMarker(const Document& document, const MarkerId& id);
// The buses a track plays into through its output bus and its sends. Disabled sends count too, so enabling one never
// closes a loop.
std::vector<TrackId> busesFedBy(const Track& track);
// True when audio from track from arrives at track to through sends and output buses.
bool reaches(const Document& document, const TrackId& from, const TrackId& to);

}  // namespace emerick::model
