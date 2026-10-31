#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "emerick/engine/audio_backend.hpp"
#include "emerick/engine/midi_backend.hpp"
#include "emerick/engine/session.hpp"
#include "polling_set.hpp"
#include "snapshot.hpp"

namespace emerick::engine {

struct BuiltSnapshot {
    std::unique_ptr<Snapshot> snapshot;
    std::vector<SessionError> errors;
};

// The UI thread builds snapshots here, so every allocation and file open happens off the audio thread. We reuse
// streams, recorders and ports while a snapshot holds them, so a swap keeps read-ahead and takes in flight.
class SnapshotBuilder {
public:
    SnapshotBuilder(MidiBackend& midi, TakeInbox& inbox, PollingSet<ClipStream>& reader,
                    PollingSet<TakeRecorder>& writer, PollingSet<MidiOutputPort>& sender);
    ~SnapshotBuilder() = default;
    SnapshotBuilder(const SnapshotBuilder&) = delete;
    SnapshotBuilder& operator=(const SnapshotBuilder&) = delete;
    SnapshotBuilder(SnapshotBuilder&&) = delete;
    SnapshotBuilder& operator=(SnapshotBuilder&&) = delete;

    // Audio takes skip their first captureOffset frames.
    BuiltSnapshot build(const Session& session, const StreamLayout& layout, Frames captureOffset);
    void prune();

    [[nodiscard]] bool clipsBuffered() const;
    [[nodiscard]] bool midiFlushed() const;
    using Meters = std::map<std::string, std::weak_ptr<LevelMeter>>;
    // The input meter of every audio track with an input, by track id.
    [[nodiscard]] const Meters& inputMeters() const { return inputMeters_; }
    // The effect of every compressor and limiter insert, by insert id.
    [[nodiscard]] const std::map<std::string, std::weak_ptr<Effect>>& compressors() const { return compressors_; }
    // The output meter of every track with an audio output, by track id.
    [[nodiscard]] const Meters& outputMeters() const { return outputMeters_; }
    [[nodiscard]] const std::map<std::string, std::weak_ptr<TakeRecorder>>& recorders() const { return recorders_; }

private:
    std::shared_ptr<MidiInputPort> inputPort(const std::string& id);
    std::shared_ptr<MidiOutputPort> outputPort(const std::string& id);
    void addClips(TrackState& state, const Track& track, const Session& session, const StreamLayout& layout,
                  std::vector<SessionError>& errors);
    std::shared_ptr<const Preload> preload(const std::string& key, const AudioClip& clip, const ClipPlayback& playback,
                                           Frames cycleStart, double sampleRate);
    std::shared_ptr<const MidiSchedule> midiSchedule(const Track& track, const core::TempoMap& tempo,
                                                     double sampleRate);
    void addRecorder(TrackState& state, const Track& track, const Session& session, const StreamLayout& layout,
                     Frames captureOffset);
    void addInserts(TrackState& state, const Track& track, const StreamLayout& layout);
    void addMetronome(Snapshot& snapshot, const Session& session, const StreamLayout& layout,
                      std::vector<SessionError>& errors);
    std::shared_ptr<const DrumSounds> drumSounds(const Metronome& metronome, double sampleRate,
                                                 std::vector<SessionError>& errors);

    MidiBackend& midi_;
    TakeInbox& inbox_;
    PollingSet<ClipStream>& reader_;
    PollingSet<TakeRecorder>& writer_;
    PollingSet<MidiOutputPort>& sender_;
    std::map<std::string, std::weak_ptr<ClipStream>> clips_;
    std::map<std::string, std::weak_ptr<const Preload>> preloads_;
    // Holding the clips keeps a later change from writing into them, so the same clips mean the same schedule.
    struct CachedSchedule {
        std::shared_ptr<const std::vector<MidiClip>> clips;
        core::TempoMap tempo;
        double sampleRate = 0.0;
        TrackKind kind = TrackKind::audio;
        std::weak_ptr<const MidiSchedule> schedule;
    };
    std::map<std::string, CachedSchedule> schedules_;
    std::map<std::string, std::weak_ptr<TakeRecorder>> recorders_;
    std::map<std::string, std::weak_ptr<Effect>> effects_;
    std::map<std::string, std::weak_ptr<Effect>> compressors_;
    Meters inputMeters_;
    Meters outputMeters_;
    std::map<std::string, std::weak_ptr<MidiInputPort>> inputs_;
    std::map<std::string, std::weak_ptr<MidiOutputPort>> outputs_;
    // The sounds depend only on these, so a snapshot keeps the sounds the last one built while they stay the same.
    std::string soundsKey_;
    std::shared_ptr<const DrumSounds> sounds_;
    std::vector<SessionError> soundErrors_;
};

}  // namespace emerick::engine
