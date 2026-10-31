#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "clip_stream.hpp"
#include "emerick/engine/engine.hpp"
#include "emerick/engine/session.hpp"
#include "metronome_sounds.hpp"
#include "midi_ports.hpp"
#include "rt_effects.hpp"
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
    // Set while a cycle starts inside the clip or just before it.
    std::shared_ptr<const Preload> preload;
};

struct ScheduledMidi {
    Frames frame = 0;
    std::size_t offset = 0;
    std::size_t size = 0;
};

// A track's MIDI clips placed on frames. Snapshots share it while the clips, the tempo and the rate stay the same.
struct MidiSchedule {
    std::vector<ScheduledMidi> events;
    // The bank, program, controller and pitch bend events among events, which a chase sends.
    std::vector<ScheduledMidi> chaseEvents;
    std::vector<std::uint8_t> bytes;
};

// The audio thread raises the peaks and the clip flag, and the UI thread reads and resets them.
struct LevelMeter {
    std::array<std::atomic<float>, kMeterChannels> peaks{};
    std::atomic<bool> clipped{false};
    // Only the UI thread touches this.
    std::size_t channels = 0;
};

struct SendState {
    std::size_t bus = 0;  // the bus's index in the snapshot's tracks
    float gain = 1.0F;
    bool pre = false;
    // The gain the last block ended on, so a send level change ramps like a fader.
    float rampFrom = 1.0F;
};

struct InsertState {
    std::shared_ptr<Effect> effect;
    EffectParams params{};
    bool bypass = false;
};

struct LanePoint {
    Frames frame = 0;
    float value = 0.0F;
    core::AutomationCurve curve = core::AutomationCurve::linear;
};

struct LaneState {
    AutomationTarget target = AutomationTarget::gain;
    std::size_t send = 0;    // the send's index in the track's sends
    std::size_t insert = 0;  // the insert's index in the track's inserts
    std::size_t param = 0;
    std::vector<LanePoint> points;
};

struct TrackState {
    std::string id;
    TrackKind kind = TrackKind::audio;
    bool armed = false;
    bool mute = false;
    bool solo = false;
    bool monitor = false;
    float gain = 1.0F;
    float trim = 1.0F;
    // The trim the last block ended on, so a trim change ramps like a fader.
    float trimFrom = 1.0F;
    float pan = 0.0F;
    // An automated mute ramps the track to silence, so it never clicks.
    float muteGain = 1.0F;
    std::vector<int> inputs;
    std::vector<int> outputs;
    std::vector<int> captures;
    std::vector<const float*> inputPointers;
    std::shared_ptr<MidiInputPort> midiIn;
    // The index of the track whose instrument's MIDI this track takes in place of midiIn.
    std::optional<std::size_t> midiSource;
    int inChannel = 0;
    std::shared_ptr<MidiOutputPort> midiOut;
    int outChannel = 0;
    bool thru = false;
    bool claimsSysex = false;
    std::vector<ClipPlayback> clips;
    // Never null.
    std::shared_ptr<const MidiSchedule> midi;
    std::array<std::bitset<kMidiNotes>, kMidiChannels> soundingNotes{};
    std::shared_ptr<TakeRecorder> recorder;
    Frames recordDelay = 0;
    std::shared_ptr<LevelMeter> inputMeter;
    std::shared_ptr<LevelMeter> outputMeter;
    // The track's signal per channel for the current block, so we apply its fader and pan and meter it before it joins
    // the mix. A bus's sources add into its mix before we process the bus.
    std::vector<std::vector<float>> mix;
    std::vector<float*> mixPointers;
    std::vector<InsertState> inserts;
    std::shared_ptr<ClapInstrument> instrument;
    // The gain per channel that the last block ended on, so we ramp from it and a fader move never clicks.
    std::vector<float> outputGains;
    std::vector<SendState> sends;
    std::vector<LaneState> lanes;
    // The index of the bus the track plays into in place of its outputs.
    std::optional<std::size_t> outputBus;
    // While any track is soloed, a track stays audible when it is soloed, a soloed track plays into it, it plays
    // into a soloed bus, or it sends MIDI to a soloed track.
    bool fedBySolo = false;
    bool feedsSolo = false;
};

// Bytes for a port, or for an instrument when port is null.
struct RestartMessage {
    std::shared_ptr<MidiOutputPort> port;
    std::shared_ptr<ClapInstrument> instrument;
    std::vector<std::uint8_t> bytes;
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
    // Track indices with every track before the buses it plays into, so a bus mixes after all its sources.
    std::vector<std::size_t> mixOrder;
    // Track indices with an instrument, each after the track it takes MIDI from, so that MIDI reaches it in the same
    // block.
    std::vector<std::size_t> instrumentOrder;
    std::vector<std::shared_ptr<MidiOutputPort>> clockPorts;
    // Every output port the tracks, the clock and the metronome use, once each.
    std::vector<std::shared_ptr<MidiOutputPort>> outputs;
    std::vector<std::shared_ptr<MidiInputPort>> inputs;
    bool sendStart = true;
    bool chaseMidi = true;
    bool clockWhileStopped = false;
    // The cycle on frames; an end at or before start plays straight on.
    Frames cycleStart = 0;
    Frames cycleEnd = 0;
    std::vector<RestartMessage> cycleRestart;
    std::vector<std::uint8_t> incoming = std::vector<std::uint8_t>(kMaxIncomingMessage);
    MetronomeState metronome;
};

inline bool anySolo(const Snapshot& snapshot) { return std::ranges::any_of(snapshot.tracks, &TrackState::solo); }

// Pan applies to a stereo pair; a track on one channel or on more than two plays at its gain on each.
inline float outputGain(const TrackState& track, std::size_t channel, PanLaw law) {
    const float gain = track.gain * track.muteGain;
    if (track.mix.size() != 2) {
        return gain;
    }
    const StereoGains pan = panGains(track.pan, law);
    return gain * (channel == 0 ? pan.left : pan.right);
}

template <class Visit>
void forEachBusFed(const TrackState& track, Visit visit) {
    for (const SendState& send : track.sends) {
        visit(send.bus);
    }
    if (track.outputBus) {
        visit(*track.outputBus);
    }
}

// Runs on the audio thread whenever a solo changes, so it only walks the mix order.
inline void updateSoloPaths(Snapshot& snapshot) {
    for (TrackState& track : snapshot.tracks) {
        track.fedBySolo = track.solo;
        track.feedsSolo = track.solo;
    }
    for (const std::size_t index : snapshot.mixOrder) {
        const TrackState& track = snapshot.tracks[index];
        if (track.fedBySolo) {
            forEachBusFed(track, [&](std::size_t bus) { snapshot.tracks[bus].fedBySolo = true; });
        }
    }
    for (const std::size_t index : snapshot.mixOrder | std::views::reverse) {
        TrackState& track = snapshot.tracks[index];
        forEachBusFed(track,
                      [&](std::size_t bus) { track.feedsSolo = track.feedsSolo || snapshot.tracks[bus].feedsSolo; });
    }  // A soloed track hears only what its MIDI source sends it, so the sources up its chain play too.
    for (std::size_t index = 0; index < snapshot.tracks.size(); ++index) {
        if (!snapshot.tracks[index].feedsSolo && !snapshot.tracks[index].fedBySolo) {
            continue;
        }
        std::optional<std::size_t> source = snapshot.tracks[index].midiSource;
        for (std::size_t hops = 0; source && hops < snapshot.tracks.size(); ++hops) {
            snapshot.tracks[*source].feedsSolo = true;
            source = snapshot.tracks[*source].midiSource;
        }
    }
}

inline bool audible(const TrackState& track, bool soloActive) {
    return !track.mute && (!soloActive || track.fedBySolo || track.feedsSolo);
}

}  // namespace emerick::engine
