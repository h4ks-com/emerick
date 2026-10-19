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

private:
    std::shared_ptr<MidiInputPort> inputPort(const std::string& id);
    std::shared_ptr<MidiOutputPort> outputPort(const std::string& id);
    void addClips(TrackState& state, const Track& track, const core::TempoMap& tempo, const StreamLayout& layout,
                  std::vector<SessionError>& errors);
    static void addMidiEvents(TrackState& state, const Track& track, const core::TempoMap& tempo, double sampleRate);
    void addRecorder(TrackState& state, const Track& track, const Session& session, const StreamLayout& layout,
                     Frames captureOffset);

    MidiBackend& midi_;
    TakeInbox& inbox_;
    PollingSet<ClipStream>& reader_;
    PollingSet<TakeRecorder>& writer_;
    PollingSet<MidiOutputPort>& sender_;
    std::map<std::string, std::weak_ptr<ClipStream>> clips_;
    std::map<std::string, std::weak_ptr<TakeRecorder>> recorders_;
    std::map<std::string, std::weak_ptr<MidiInputPort>> inputs_;
    std::map<std::string, std::weak_ptr<MidiOutputPort>> outputs_;
};

}  // namespace emerick::engine
