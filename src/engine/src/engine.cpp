// The reader thread fills every clip's ring ahead of the playhead.
// The writer thread writes recorded audio to WAV files and collects recorded MIDI.
// The sender thread sends every queued MIDI message when its timestamp is due.

#include "emerick/engine/engine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "clock.hpp"
#include "render_pipeline.hpp"
#include "rt_process.hpp"
#include "snapshot_builder.hpp"

namespace emerick::engine {

namespace {

void addOnce(std::vector<std::string>& ids, const std::string& id) {
    if (!id.empty() && !std::ranges::contains(ids, id)) {
        ids.push_back(id);
    }
}

bool listensToAudioInput(const Track& track) { return track.armed || track.monitor; }

// Live MIDI and clock while stopped pass through the audio callback, so they keep the stream running as an armed
// track does.
bool listensLive(const Session& session) {
    return (session.clockWhileStopped && !session.clockPorts.empty()) ||
           std::ranges::any_of(session.tracks, [](const Track& track) {
               return listensToAudioInput(track) || !track.midiIn.portId.empty() || !track.midiInTrack.empty();
           });
}

bool sameDevices(const StreamRequest& left, const StreamRequest& right) {
    return left.deviceIds == right.deviceIds && left.inputDeviceIds == right.inputDeviceIds;
}

void deleteSnapshot(Snapshot* snapshot) { std::unique_ptr<Snapshot>{snapshot}.reset(); }

void deletePreview(PreviewSound* preview) { std::unique_ptr<PreviewSound>{preview}.reset(); }

std::vector<LevelReading> takeLevels(const SnapshotBuilder::Meters& meters) {
    std::vector<LevelReading> readings;
    for (const auto& [trackId, weak] : meters) {
        const std::shared_ptr<LevelMeter> meter = weak.lock();
        if (!meter) {
            continue;
        }
        readings.push_back({trackId,
                            meter->peaks | std::views::take(meter->channels) | std::views::transform([](auto& peak) {
                                return peak.exchange(0.0F, std::memory_order_relaxed);
                            }) | std::ranges::to<std::vector>(),
                            meter->clipped.exchange(false, std::memory_order_relaxed)});
    }
    return readings;
}

}  // namespace

struct Engine::Impl {
    Impl(std::unique_ptr<AudioBackend> audioBackend, std::unique_ptr<MidiBackend> midiBackend, Options engineOptions)
        : audio(std::move(audioBackend)), midi(std::move(midiBackend)), options(engineOptions) {
        if (options.pipelineBuses) {
            pipeline = std::make_unique<RenderPipeline>();
            rt.pipeline = pipeline.get();
        }
    }

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
    // The devices layout describes, open or not.
    std::optional<StreamRequest> layoutRequest;
    bool streamOpen = false;
    bool transportRequested = false;
    bool warm = false;
    std::vector<SessionError> errors;
    Frames compensation = 0;
    std::uint64_t locatesRequested = 0;
    std::unique_ptr<RenderPipeline> pipeline;
    RealtimeState rt;
    std::atomic<std::uint64_t> callbacks{0};
    std::uint64_t seenCallbacks = 0;
    std::optional<std::chrono::steady_clock::time_point> calledBackAt;
    bool stalled = false;

    PollingSet<ClipStream> reader{std::chrono::milliseconds(2)};
    PollingSet<TakeRecorder> writer{std::chrono::milliseconds(5)};
    PollingSet<MidiOutputPort> sender{std::chrono::microseconds(500)};
    SnapshotBuilder builder{*midi, inbox, reader, writer, sender};

    // NOLINTNEXTLINE(bugprone-exception-escape): a semaphore that cannot wait ends the process.
    ~Impl() {
        if (streamOpen) {
            audio->close();
        }
        stopBuses();
        writer.pollAll();
        deleteSnapshot(rt.current);
        deleteSnapshot(rt.pending.exchange(nullptr));
        deletePreview(rt.preview);
        deletePreview(rt.pendingPreview.exchange(nullptr));
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
        PreviewSound* preview = nullptr;
        while (rt.retiredPreviews.try_dequeue(preview)) {
            deletePreview(preview);
        }
    }

    [[nodiscard]] bool previewPlaying() const {
        return rt.previewSounding.load(std::memory_order_acquire) ||
               rt.metronomePreviewing.load(std::memory_order_acquire) ||
               rt.pendingPreview.load(std::memory_order_acquire) != nullptr;
    }

    void previewMetronome() {
        rt.metronomePreviewing.store(true, std::memory_order_release);
        if (!streamOpen) {
            openStream(requestFor(session), session);
        }
        push({.type = Command::Type::previewMetronome});
    }

    void playPreview(std::string_view trackId, std::vector<std::vector<float>> channels) {
        if (channels.empty() || channels.front().empty()) {
            return;
        }
        auto sound = std::make_unique<PreviewSound>(
            PreviewSound{.trackId = std::string{trackId}, .channels = std::move(channels), .position = 0});
        deletePreview(rt.pendingPreview.exchange(sound.release(), std::memory_order_acq_rel));
        if (!streamOpen) {
            openStream(requestFor(session), session);
        }
        freeRetired();
    }

    void push(const Command& command) {
        if (!rt.commands.try_enqueue(command)) {
            throw std::runtime_error("engine command queue is full");
        }
        if (!streamOpen) {
            runIdleBlock();
        }
    }

    // With the stream closed the audio thread is gone, so the UI thread applies commands and swaps snapshots in with
    // a block of no frames.
    void runIdleBlock() {
        float unused = 0.0F;
        const std::vector<const float*> inputs(static_cast<std::size_t>(layout.inputChannels), &unused);
        const std::vector<float*> outputs(static_cast<std::size_t>(layout.outputChannels), &unused);
        processBlock(rt, {0, steadyNowNs(), inputs, outputs});
        freeRetired();
    }

    // Returns the session's track, which keeps the change for the snapshot we build when the stream opens again.
    Track& pushTrack(Command command, std::string_view trackId) {
        const auto track = std::ranges::find(session.tracks, trackId, &Track::id);
        if (track == session.tracks.end()) {
            throw std::invalid_argument("unknown track " + std::string(trackId));
        }
        std::ranges::copy(trackId, command.trackId.begin());
        command.trackIdLength = trackId.size();
        push(command);
        return *track;
    }

    void setSession(Session next) {
        std::set<std::string> ids;
        for (const Track& track : next.tracks) {
            if (track.id.empty() || track.id.size() > kMaxTrackIdLength || !ids.insert(track.id).second) {
                throw std::invalid_argument("track ids must be unique and 1 to 64 characters: " + track.id);
            }
        }
        for (Track& track : next.tracks) {
            if (!listensToAudioInput(track)) {
                track.audioIn = {};
            }
        }
        const StreamRequest request = requestFor(next);
        checkRates(request.deviceIds);
        const bool tempoChanged = next.tempo != session.tempo;
        const Ticks playhead =
            session.tempo.framesToTicks(rt.publishedFrame.load(std::memory_order_relaxed), layout.sampleRate);
        const bool changed = !layoutRequest || !sameDevices(request, *layoutRequest);
        if (changed && streamOpen) {
            closeStream();
        }
        if (!streamOpen && (transportRequested || warm || listensLive(next))) {
            openStream(request, next);
        } else {
            if (changed) {
                useLayout(concatenateDevices(audioDevices, request), request);
            }
            publishSnapshot(next);
        }
        session = std::move(next);
        builder.prune();
        if (tempoChanged) {
            push({.type = Command::Type::locate, .ticks = playhead});
            ++locatesRequested;
        }
    }

    void sendMidi(std::string_view targetId, std::span<const std::uint8_t> bytes, bool toInstrument) {
        if (targetId.size() + bytes.size() > kMaxIncomingMessage) {
            throw std::invalid_argument("a MIDI message and its port id must fit in 64 KiB");
        }
        std::vector<std::uint8_t> record(targetId.begin(), targetId.end());
        record.insert(record.end(), bytes.begin(), bytes.end());
        if (!writeRecord(rt.outgoingMidi,
                         OutgoingMidiHeader{.targetIdSize = static_cast<std::uint32_t>(targetId.size()),
                                            .size = static_cast<std::uint32_t>(bytes.size()),
                                            .toInstrument = toInstrument},
                         std::span<const std::uint8_t>(record))) {
            throw std::runtime_error("engine MIDI queue is full");
        }
        if (!streamOpen) {
            runIdleBlock();
        }
    }

    // The system can leave a stream that combines devices with a longer delay after an overload, and only a fresh
    // stream brings it back down.
    void panic() {
        if (streamOpen) {
            audio->close();
            streamOpen = false;
            push({.type = Command::Type::panic});
            openStream(requestFor(session), session);
            return;
        }
        push({.type = Command::Type::panic});
    }

    void play() {
        if (!streamOpen) {
            openStream(requestFor(session), session);
        }
        transportRequested = true;
        push({.type = Command::Type::play});
    }

    void stop() {
        transportRequested = false;
        push({.type = Command::Type::stop});
    }

    void keepWarm(bool value) {
        warm = value;
        if (const StreamRequest request = requestFor(session); warm && !streamOpen && !request.deviceIds.empty()) {
            openStream(request, session);
        }
    }

    void releaseIdleStream() {
        if (streamOpen && !warm && !transportRequested && !rt.publishedPlaying.load(std::memory_order_relaxed) &&
            !listensLive(session) && builder.midiFlushed() && !previewPlaying()) {
            audio->close();
            streamOpen = false;
            runIdleBlock();
        }
    }

    bool watchStream(std::chrono::steady_clock::time_point now) {
        const std::uint64_t seen = callbacks.load(std::memory_order_relaxed);
        const bool watched = streamOpen && (transportRequested || listensLive(session));
        if (seen != seenCallbacks) {
            stalled = false;
        }
        if (!watched || seen != seenCallbacks || !calledBackAt) {
            seenCallbacks = seen;
            calledBackAt = watched ? std::optional(now) : std::nullopt;
            return false;
        }
        if (now - *calledBackAt < kStreamStall) {
            return false;
        }
        stalled = true;
        audio->close();
        streamOpen = false;
        stop();
        return true;
    }

    // Outputs come first, so a combined device takes its clock from an output.
    StreamRequest requestFor(const Session& next) const {
        StreamRequest request{.deviceIds = {},
                              .sampleRate = options.sampleRate,
                              .blockFrames = options.blockFrames,
                              .available = audioDevices};
        for (const Track& track : next.tracks) {
            addOnce(request.deviceIds, track.audioOut.deviceId);
            addOnce(request.deviceIds, track.captureOut.deviceId);
            addOnce(request.deviceIds, track.chainOut.deviceId);
        }
        addOnce(request.deviceIds, next.metronome.audioOut.deviceId);
        for (const Track& track : next.tracks) {
            addOnce(request.deviceIds, track.audioIn.deviceId);
            addOnce(request.inputDeviceIds, track.audioIn.deviceId);
        }
        return request;
    }

    void publishSnapshot(const Session& next) {
        BuiltSnapshot built =
            builder.build(next, layout, layout.inputLatency + layout.outputLatency + options.recordingDelay);
        deleteSnapshot(rt.pending.exchange(built.snapshot.release(), std::memory_order_acq_rel));
        errors = std::move(built.errors);
        compensation = built.compensation;
        if (!streamOpen) {
            runIdleBlock();
        }
        freeRetired();
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

    void closeStream() {
        audio->close();
        streamOpen = false;
        dropSnapshots();
    }

    // The bus thread of an offline render can still mix the current snapshot, so we wait for it before a snapshot goes.
    void stopBuses() const {
        if (pipeline) {
            pipeline->wait();
        }
    }

    void dropSnapshots() {
        stopBuses();
        deleteSnapshot(std::exchange(rt.current, nullptr));
        deleteSnapshot(rt.pending.exchange(nullptr, std::memory_order_acq_rel));
        freeRetired();
    }

    // The callback can run as soon as open starts the stream, so we take every snapshot built for the old layout away
    // from it first; until we publish one the callback leaves the playhead alone. We keep them until the new snapshot
    // holds their clip streams, so the read-ahead survives.
    void openStream(const StreamRequest& request, const Session& next) {
        stopBuses();
        std::array<std::unique_ptr<Snapshot>, 2> old{
            std::unique_ptr<Snapshot>{std::exchange(rt.current, nullptr)},
            std::unique_ptr<Snapshot>{rt.pending.exchange(nullptr, std::memory_order_acq_rel)}};
        const Frames closedFrames = layout.maxFrames;
        try {
            useLayout(audio->open(request,
                                  [this](const AudioBlock& block) {
                                      callbacks.fetch_add(1, std::memory_order_relaxed);
                                      processBlock(rt, block);
                                  }),
                      request);
        } catch (const std::runtime_error&) {
            restoreClosedSnapshots(old);
            throw;
        } catch (const std::invalid_argument&) {
            restoreClosedSnapshots(old);
            throw;
        }
        streamOpen = true;
        calledBackAt.reset();
        // Clip streams and recorders size their buffers for one block, so a larger device block needs new ones.
        if (layout.maxFrames > closedFrames) {
            old = {};
        }
        try {
            publishSnapshot(next);
        } catch (const std::invalid_argument&) {
            closeStream();
            throw;
        } catch (const std::runtime_error&) {
            closeStream();
            throw;
        }
    }

    // The stream stays closed, so the idle block keeps applying commands to the snapshots we took away.
    void restoreClosedSnapshots(std::array<std::unique_ptr<Snapshot>, 2>& old) {
        rt.current = old[0].release();
        rt.pending.store(old[1].release(), std::memory_order_release);
    }

    // The default output device can run at another rate than the last layout, so we keep the playhead on its tick.
    void useLayout(StreamLayout next, const StreamRequest& request) {
        if (next.sampleRate != layout.sampleRate) {
            const Ticks at = session.tempo.framesToTicks(rt.frame, layout.sampleRate);
            rt.frame = session.tempo.ticksToFrames(at, next.sampleRate);
            rt.publishedFrame.store(rt.frame, std::memory_order_relaxed);
        }
        layout = std::move(next);
        layoutRequest = request;
        if (pipeline) {
            pipeline->prepare(static_cast<std::size_t>(layout.outputChannels), layout.maxFrames);
        }
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

void Engine::releaseIdleStream() { impl_->releaseIdleStream(); }

void Engine::keepWarm(bool value) { impl_->keepWarm(value); }

void Engine::play() { impl_->play(); }

void Engine::stop() { impl_->stop(); }

void Engine::locate(Ticks ticks) {
    impl_->push({.type = Command::Type::locate, .ticks = ticks});
    ++impl_->locatesRequested;
}

void Engine::sendMidi(std::string_view portId, std::span<const std::uint8_t> bytes) {
    impl_->sendMidi(portId, bytes, false);
}

void Engine::sendInstrumentMidi(std::string_view trackId, std::span<const std::uint8_t> bytes) {
    impl_->sendMidi(trackId, bytes, true);
}

void Engine::previewMetronome() { impl_->previewMetronome(); }

void Engine::playPreview(std::string_view trackId, std::vector<std::vector<float>> channels) {
    impl_->playPreview(trackId, std::move(channels));
}

bool Engine::previewPlaying() const { return impl_->previewPlaying(); }

void Engine::panic() { impl_->panic(); }

void Engine::setRecording(bool on) { impl_->push({.type = Command::Type::record, .on = on}); }

void Engine::setMute(std::string_view trackId, bool mute) {
    impl_->pushTrack({.type = Command::Type::mute, .on = mute}, trackId).mute = mute;
}

void Engine::setSolo(std::string_view trackId, bool solo) {
    impl_->pushTrack({.type = Command::Type::solo, .on = solo}, trackId).solo = solo;
}

void Engine::setArmed(std::string_view trackId, bool armed) {
    impl_->pushTrack({.type = Command::Type::arm, .on = armed}, trackId).armed = armed;
}

void Engine::setGain(std::string_view trackId, float gain) {
    impl_->pushTrack({.type = Command::Type::gain, .gain = gain}, trackId).gain = gain;
}

void Engine::setPan(std::string_view trackId, float pan) {
    impl_->pushTrack({.type = Command::Type::pan, .pan = pan}, trackId).pan = pan;
}

TransportPosition Engine::position() const {
    const Frames frames = impl_->rt.publishedFrame.load(std::memory_order_relaxed);
    return {frames, impl_->session.tempo.framesToTicks(frames, impl_->layout.sampleRate)};
}

bool Engine::playing() const { return impl_->rt.publishedPlaying.load(std::memory_order_relaxed); }

const StreamLayout& Engine::layout() const { return impl_->layout; }

bool Engine::streamOpen() const { return impl_->streamOpen; }

bool Engine::watchStream(std::chrono::steady_clock::time_point now) { return impl_->watchStream(now); }

bool Engine::streamStalled() const { return impl_->stalled; }

const std::vector<SessionError>& Engine::sessionErrors() const { return impl_->errors; }

Frames Engine::latencyCompensation() const { return impl_->compensation; }

Frames Engine::outputDelay() const { return impl_->pipeline ? impl_->pipeline->delay() : 0; }

bool Engine::readAheadReady() const {
    return impl_->rt.locatesApplied.load(std::memory_order_acquire) == impl_->locatesRequested &&
           impl_->builder.clipsBuffered();
}

bool Engine::midiFlushed() const { return impl_->builder.midiFlushed(); }

std::vector<RecordedTake> Engine::takeRecordings() {
    impl_->freeRetired();
    return impl_->inbox.take();
}

void Engine::finishEndedTakes() { impl_->writer.pollAll(); }

std::vector<TakeProgress> Engine::takeProgress() {
    std::vector<TakeProgress> progress;
    for (const auto& entry : impl_->builder.recorders()) {
        const std::shared_ptr<TakeRecorder> recorder = entry.second.lock();
        if (auto read = recorder ? recorder->takeProgress() : std::nullopt) {
            progress.push_back(*std::move(read));
        }
    }
    return progress;
}

std::vector<LevelReading> Engine::takeInputLevels() { return takeLevels(impl_->builder.inputMeters()); }

std::vector<LevelReading> Engine::takeOutputLevels() { return takeLevels(impl_->builder.outputMeters()); }

std::vector<GainReduction> Engine::takeGainReductions() {
    std::vector<GainReduction> readings;
    for (const auto& [insertId, effect] : impl_->builder.meteredInserts()) {
        if (const std::shared_ptr<Effect> alive = effect.lock()) {
            readings.push_back({insertId, alive->takeReductionDb()});
        }
    }
    return readings;
}

AudioBackend& Engine::audio() { return *impl_->audio; }

MidiBackend& Engine::midi() { return *impl_->midi; }

}  // namespace emerick::engine
