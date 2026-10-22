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
    // The track's signal per channel for the current block, so we apply its fader and pan and meter it before it joins
    // the mix. A bus's sources add into its mix before we process the bus.
    std::vector<std::vector<float>> mix;
    std::vector<float*> mixPointers;
    std::vector<InsertState> inserts;
    // The gain per channel that the last block ended on, so we ramp from it and a fader move never clicks.
    std::vector<float> outputGains;
    std::vector<SendState> sends;
    // The index of the bus the track plays into in place of its outputs.
    std::optional<std::size_t> outputBus;
    // While any track is soloed, a track stays audible when it is soloed, a soloed track plays into it, or it plays
    // into a soloed bus.
    bool fedBySolo = false;
    bool feedsSolo = false;
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
    std::vector<std::shared_ptr<MidiOutputPort>> clockPorts;
    std::vector<std::shared_ptr<MidiInputPort>> inputs;
    std::vector<std::uint8_t> incoming = std::vector<std::uint8_t>(kMaxIncomingMessage);
    MetronomeState metronome;
    // Output pointers for the part of a block after a count-in ends, one per stream output.
    std::vector<float*> tailOutputs;
};

inline bool anySolo(const Snapshot& snapshot) { return std::ranges::any_of(snapshot.tracks, &TrackState::solo); }

// Pan applies to a stereo pair; a track on one channel or on more than two plays at its gain on each.
inline float outputGain(const TrackState& track, std::size_t channel, PanLaw law) {
    if (track.mix.size() != 2) {
        return track.gain;
    }
    const StereoGains pan = panGains(track.pan, law);
    return track.gain * (channel == 0 ? pan.left : pan.right);
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
    }
}

inline bool audible(const TrackState& track, bool soloActive) {
    return !track.mute && (!soloActive || track.fedBySolo || track.feedsSolo);
}

}  // namespace emerick::engine
