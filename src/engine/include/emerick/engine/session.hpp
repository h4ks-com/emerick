#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "emerick/core/automation.hpp"
#include "emerick/core/fade.hpp"
#include "emerick/core/shared_vector.hpp"
#include "emerick/core/time.hpp"
#include "emerick/engine/effects.hpp"
#include "emerick/engine/plugin.hpp"

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
    // A clip whose last event starts something longer, such as a style Ending, plays until start + length.
    Ticks length = 0;
};

// A pre-fader send taps the track before its fader and pan, a post-fader send after them.
struct Send {
    std::string bus;
    float gain = 1.0F;
    bool pre = false;
    bool operator==(const Send&) const = default;
};

// An insert effect on a track's signal before its fader, pan and sends. Its id stays the same across sessions, so the
// effect keeps its state while the insert lives. An insert with a plugin plays the plugin in place of a built-in
// effect; the app owns the plugin, and a snapshot shares it until it retires on the UI thread.
struct Insert {
    std::string id;
    EffectType type = EffectType::eq;
    bool bypass = false;
    EffectParams params{};
    std::shared_ptr<Plugin> plugin;
};

// The parameter a lane moves. Gain and send levels are in dB, pan runs from -1 to 1 and mute is on from 0.5.
enum class AutomationTarget : std::uint8_t { gain, pan, mute, send, insert };

struct AutomationPoint {
    Ticks tick = 0;
    float value = 0.0F;
    core::AutomationCurve curve = core::AutomationCurve::linear;
};

// A send lane names the bus of the send it moves. While a lane plays, it overrides the track's gain, pan, mute or
// send level.
struct AutomationLane {
    AutomationTarget target = AutomationTarget::gain;
    std::string bus;
    std::vector<AutomationPoint> points;  // sorted by tick
    // An insert lane names the insert and the index of the param it moves in the effect's table, or for a plugin insert
    // the plugin's param id, with values from Plugin::processValue.
    std::string insert;
    std::size_t param = 0;
};

struct Track {
    std::string id;
    TrackKind kind = TrackKind::audio;
    bool armed = false;
    bool mute = false;
    bool solo = false;
    bool monitor = false;
    float gain = 1.0F;
    // Scales the track's signal before its inserts and fader.
    float trim = 1.0F;
    float pan = 0.0F;  // -1 hard left, 0 center, 1 hard right
    AudioRoute audioIn;
    AudioRoute audioOut;
    MidiRoute midiIn;
    // A track id here takes the MIDI that track's instrument sends in place of midiIn's port, on midiIn's channel.
    std::string midiInTrack;
    MidiRoute midiOut;
    bool midiThru = false;
    bool claimsSysex = false;
    // Moves the track's takes later by this many milliseconds, or earlier when negative.
    double recordDelayMs = 0.0;
    // A track with an output bus plays into that bus in place of audioOut.
    std::string outputBus;
    // The track's own output, after its fader and pan, also plays here, whether it goes to audioOut or a bus.
    AudioRoute captureOut;
    std::vector<Send> sends;
    std::vector<Insert> inserts;
    std::vector<AudioClip> audioClips;
    // Sessions share the clips, so the snapshot builder reuses a track's schedule while its clips stay the same.
    core::SharedVector<MidiClip> midiClips;
    std::vector<AutomationLane> automation;
    // A MIDI track with an instrument plays its MIDI through it, and its audio mixes like an audio track's. The app
    // owns the instrument; a snapshot shares it until it retires on the UI thread.
    std::shared_ptr<Plugin> instrument;
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

// Bytes for one output port, which the session must use, or for the instrument of the track trackId names.
struct PortMessage {
    std::string portId;
    std::vector<std::uint8_t> bytes;
    std::string trackId;
    bool operator==(const PortMessage&) const = default;
};

// While playback without recording crosses end, it jumps to start sample-accurately. At each jump we send Stop to the
// clock ports, release held notes, send restart, chase MIDI at start and send Start, so a device that ignores Song
// Position Pointer starts over in its state at start.
struct Cycle {
    Ticks start = 0;
    Ticks end = 0;
    std::vector<PortMessage> restart;
    bool operator==(const Cycle&) const = default;
};

struct Session {
    core::TempoMap tempo;
    std::vector<Track> tracks;
    std::vector<std::string> clockPorts;  // MIDI outputs that get clock, Start and Stop
    std::filesystem::path takeFolder;     // recorded audio takes are written here
    Metronome metronome;
    PanLaw panLaw = PanLaw::compensated;
    // Play and a locate while playing send each audible MIDI track's last bank, program, controllers and pitch bend
    // before the playhead, so its device starts in the state the song left it in there.
    bool chaseMidi = true;
    // Clock ports get clock while stopped too, so a device that follows it runs at the project tempo.
    bool clockWhileStopped = false;
    // Play sends Start to the clock ports, which a device that starts on Start needs to withhold.
    bool sendStart = true;
    // An end at or before start plays straight on.
    Cycle cycle = {};
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

// A take in progress reports its peaks over this many frames, the finest level the app's peak cache keeps.
inline constexpr Frames kTakePeakFrames = 256;

// What a take in progress gained since the last reading, so the app can draw it growing.
struct TakeProgress {
    std::string trackId;
    std::uint64_t take = 0;  // grows with every take the track starts
    Ticks startTick = 0;
    std::filesystem::path audioFile;
    Frames frames = 0;                  // audio frames in the file so far
    std::vector<MidiEvent> midiEvents;  // ticks relative to startTick
    // The lowest and highest sample of every kTakePeakFrames frames, over all channels.
    std::vector<float> peakMinimum;
    std::vector<float> peakMaximum;
};

}  // namespace emerick::engine
