// The reader thread fills every clip's ring ahead of the playhead.
// The writer thread writes recorded audio to WAV files and collects recorded MIDI.
// The sender thread sends every queued MIDI message when its timestamp is due.

#include "emerick/engine/engine.hpp"

#include <algorithm>
#include <chrono>
#include <set>
#include <stdexcept>

#include "rt_process.hpp"
#include "snapshot_builder.hpp"

namespace emerick::engine {

namespace {

std::vector<std::string> devicesUsedBy(const Session& session) {
    std::vector<std::string> ids;
    for (const Track& track : session.tracks) {
        for (const std::string& id : {track.audioIn.deviceId, track.audioOut.deviceId}) {
            if (!id.empty() && std::ranges::find(ids, id) == ids.end()) {
                ids.push_back(id);
            }
        }
    }
    return ids;
}

void deleteSnapshot(Snapshot* snapshot) { std::unique_ptr<Snapshot>{snapshot}.reset(); }

}  // namespace

struct Engine::Impl {
    Impl(std::unique_ptr<AudioBackend> audioBackend, std::unique_ptr<MidiBackend> midiBackend, Options engineOptions)
        : audio(std::move(audioBackend)), midi(std::move(midiBackend)), options(engineOptions) {}

    std::unique_ptr<AudioBackend> audio;
    std::unique_ptr<MidiBackend> midi;
    Options options;
    TakeInbox inbox;

    std::vector<AudioDeviceInfo> audioDevices;
    std::vector<MidiPortInfo> midiPorts;
    std::uint64_t audioDevicesVersion = 0;
    std::uint64_t midiPortsVersion = 0;

    Session session;
    StreamLayout layout;
    std::vector<std::string> openDevices;
    bool streamOpen = false;
    std::vector<SessionError> errors;
    std::uint64_t locatesRequested = 0;
    RealtimeState rt;

    PollingSet<ClipStream> reader{std::chrono::milliseconds(2)};
    PollingSet<TakeRecorder> writer{std::chrono::milliseconds(5)};
    PollingSet<MidiOutputPort> sender{std::chrono::microseconds(500)};
    SnapshotBuilder builder{*midi, inbox, reader, writer, sender};

    ~Impl() {
        if (streamOpen) {
            audio->close();
        }
        writer.pollAll();
        deleteSnapshot(rt.current);
        deleteSnapshot(rt.pending.exchange(nullptr));
        freeRetired();
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    void freeRetired() {
        Snapshot* snapshot = nullptr;
        while (rt.retired.try_dequeue(snapshot)) {
            deleteSnapshot(snapshot);
        }
    }

    void push(const Command& command) {
        if (!rt.commands.try_enqueue(command)) {
            throw std::runtime_error("engine command queue is full");
        }
    }

    void pushTrack(Command command, std::string_view trackId) {
        if (std::ranges::find(session.tracks, trackId, &Track::id) == session.tracks.end()) {
            throw std::invalid_argument("unknown track " + std::string(trackId));
        }
        std::ranges::copy(trackId, command.trackId.begin());
        command.trackIdLength = trackId.size();
        push(command);
    }

    void setSession(Session next) {
        std::set<std::string> ids;
        for (const Track& track : next.tracks) {
            if (track.id.empty() || track.id.size() > kMaxTrackIdLength || !ids.insert(track.id).second) {
                throw std::invalid_argument("track ids must be unique and 1 to 64 characters: " + track.id);
            }
        }
        const std::vector<std::string> devices = devicesUsedBy(next);
        if (!streamOpen || devices != openDevices) {
            checkRates(devices);
            reopen(devices);
        }
        BuiltSnapshot built = builder.build(next, layout);
        deleteSnapshot(rt.pending.exchange(built.snapshot.release(), std::memory_order_acq_rel));
        session = std::move(next);
        errors = std::move(built.errors);
        freeRetired();
        builder.prune();
    }

    void refreshDevices() {
        audioDevicesVersion = audio->deviceListVersion();
        midiPortsVersion = midi->portListVersion();
        audioDevices = audio->devices();
        midiPorts = midi->ports();
    }

    void checkRates(const std::vector<std::string>& devices) const {
        for (const std::string& id : devices) {
            const auto device = std::ranges::find(audioDevices, id, &AudioDeviceInfo::id);
            if (device == audioDevices.end()) {
                continue;
            }
            if (const auto problem = unsupportedRate(*device, options.sampleRate)) {
                throw std::invalid_argument(*problem);
            }
        }
    }

    // The callback can run as soon as open starts the stream, so we drop every snapshot built for the old layout first.
    void reopen(const std::vector<std::string>& devices) {
        if (streamOpen) {
            audio->close();
            streamOpen = false;
        }
        deleteSnapshot(std::exchange(rt.current, nullptr));
        deleteSnapshot(rt.pending.exchange(nullptr, std::memory_order_acq_rel));
        freeRetired();
        layout = audio->open({devices, options.sampleRate, options.blockFrames, audioDevices},
                             [this](const AudioBlock& block) { processBlock(rt, block); });
        openDevices = devices;
        streamOpen = true;
    }
};

Engine::Engine(std::unique_ptr<AudioBackend> audio, std::unique_ptr<MidiBackend> midi, Options options)
    : impl_(std::make_unique<Impl>(std::move(audio), std::move(midi), options)) {
    impl_->refreshDevices();
}

Engine::~Engine() = default;

void Engine::setSession(Session session) { impl_->setSession(std::move(session)); }

const std::vector<AudioDeviceInfo>& Engine::audioDevices() const { return impl_->audioDevices; }

const std::vector<MidiPortInfo>& Engine::midiPorts() const { return impl_->midiPorts; }

bool Engine::devicesChanged() const {
    return impl_->audio->deviceListVersion() != impl_->audioDevicesVersion ||
           impl_->midi->portListVersion() != impl_->midiPortsVersion;
}

void Engine::refreshDevices() { impl_->refreshDevices(); }

void Engine::play() { impl_->push({.type = Command::Type::play}); }

void Engine::stop() { impl_->push({.type = Command::Type::stop}); }

void Engine::locate(Ticks ticks) {
    impl_->push({.type = Command::Type::locate, .ticks = ticks});
    ++impl_->locatesRequested;
}

void Engine::setRecording(bool on) { impl_->push({.type = Command::Type::record, .on = on}); }

void Engine::setMute(std::string_view trackId, bool mute) {
    impl_->pushTrack({.type = Command::Type::mute, .on = mute}, trackId);
}

void Engine::setSolo(std::string_view trackId, bool solo) {
    impl_->pushTrack({.type = Command::Type::solo, .on = solo}, trackId);
}

void Engine::setArmed(std::string_view trackId, bool armed) {
    impl_->pushTrack({.type = Command::Type::arm, .on = armed}, trackId);
}

void Engine::setGain(std::string_view trackId, float gain) {
    impl_->pushTrack({.type = Command::Type::gain, .gain = gain}, trackId);
}

TransportPosition Engine::position() const {
    const Frames frames = impl_->rt.publishedFrame.load(std::memory_order_relaxed);
    return {frames, impl_->session.tempo.framesToTicks(frames, impl_->layout.sampleRate)};
}

bool Engine::playing() const { return impl_->rt.publishedPlaying.load(std::memory_order_relaxed); }

const StreamLayout& Engine::layout() const { return impl_->layout; }

const std::vector<SessionError>& Engine::sessionErrors() const { return impl_->errors; }

bool Engine::readAheadReady() const {
    return impl_->rt.locatesApplied.load(std::memory_order_acquire) == impl_->locatesRequested &&
           impl_->builder.clipsBuffered();
}

bool Engine::midiFlushed() const { return impl_->builder.midiFlushed(); }

std::vector<RecordedTake> Engine::takeRecordings() {
    impl_->freeRetired();
    return impl_->inbox.take();
}

AudioBackend& Engine::audio() { return *impl_->audio; }

MidiBackend& Engine::midi() { return *impl_->midi; }

}  // namespace emerick::engine
