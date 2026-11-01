#include "snapshot_builder.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

#include "emerick/core/midi.hpp"

namespace emerick::engine {

namespace {

constexpr double kMillisecondsPerSecond = 1000.0;
// The reader thread polls every 2 ms, so this leaves it ample time to fill a stream after a cycle jump.
constexpr double kPreloadSeconds = 0.25;

template <class T, class Make>
std::shared_ptr<T> cached(std::map<std::string, std::weak_ptr<T>>& cache, const std::string& key, Make make) {
    if (std::shared_ptr<T> alive = cache[key].lock()) {
        return alive;
    }
    std::shared_ptr<T> made = make();
    cache[key] = made;
    return made;
}

std::shared_ptr<LevelMeter> meter(SnapshotBuilder::Meters& meters, const std::string& trackId, std::size_t channels) {
    std::shared_ptr<LevelMeter> found = cached(meters, trackId, [] { return std::make_shared<LevelMeter>(); });
    found->channels = std::min(channels, kMeterChannels);
    return found;
}

std::vector<int> channelIndices(const std::string& owner, const AudioRoute& route, const StreamLayout& layout,
                                bool input) {
    std::vector<int> indices;
    for (const int channel : route.channels) {
        const auto index =
            input ? layout.inputIndex(route.deviceId, channel) : layout.outputIndex(route.deviceId, channel);
        if (!index) {
            throw std::invalid_argument(owner + ": " + route.deviceId + " has no " + (input ? "input" : "output") +
                                        " channel " + std::to_string(channel));
        }
        indices.push_back(*index);
    }
    return indices;
}

// A track has one channel per output. One that plays into a bus is stereo, and so is a bus or a sending track
// without outputs, so its sends still play.
std::size_t mixChannels(const Track& track, const TrackState& state) {
    constexpr std::size_t kBusChannels = 2;
    if (!track.outputBus.empty()) {
        return kBusChannels;
    }
    if (!state.outputs.empty()) {
        return state.outputs.size();
    }
    if (!state.captures.empty()) {
        return state.captures.size();
    }
    return track.kind == TrackKind::bus || !track.sends.empty() ? kBusChannels : 0;
}

// Resolves sends and output buses to bus indices and orders the tracks so every bus mixes after its sources. A loop
// the model let through loses its routes into buses, so no bus waits on itself.
void connectBuses(Snapshot& snapshot, const Session& session, std::vector<SessionError>& errors) {
    const std::size_t count = session.tracks.size();
    const auto busIndex = [&](const std::string& id, std::size_t from) -> std::optional<std::size_t> {
        const auto found = std::ranges::find(session.tracks, id, &Track::id);
        const auto index = static_cast<std::size_t>(found - session.tracks.begin());
        if (found == session.tracks.end() || found->kind != TrackKind::bus || index == from) {
            return std::nullopt;
        }
        return index;
    };
    for (std::size_t index = 0; index < count; ++index) {
        const Track& track = session.tracks[index];
        TrackState& state = snapshot.tracks[index];
        if (state.mix.empty()) {
            continue;
        }
        state.outputBus = busIndex(track.outputBus, index);
        for (const Send& send : track.sends) {
            if (const auto bus = busIndex(send.bus, index)) {
                state.sends.push_back({.bus = *bus, .gain = send.gain, .pre = send.pre, .rampFrom = send.gain});
            }
        }
    }
    std::vector<std::size_t> sources(count);
    for (const TrackState& state : snapshot.tracks) {
        forEachBusFed(state, [&](std::size_t bus) { ++sources[bus]; });
    }
    snapshot.mixOrder = std::views::iota(std::size_t{0}, count) |
                        std::views::filter([&](std::size_t index) { return sources[index] == 0; }) |
                        std::ranges::to<std::vector>();
    for (std::size_t position = 0; position < snapshot.mixOrder.size(); ++position) {
        forEachBusFed(snapshot.tracks[snapshot.mixOrder[position]], [&](std::size_t bus) {
            if (--sources[bus] == 0) {
                snapshot.mixOrder.push_back(bus);
            }
        });
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (sources[index] > 0) {
            TrackState& state = snapshot.tracks[index];
            state.sends.clear();
            state.outputBus.reset();
            snapshot.mixOrder.push_back(index);
            errors.push_back({state.id, "its sends feed back into it, so they stay silent"});
        }
    }
}

bool inMidiLoop(const Snapshot& snapshot, std::size_t index) {
    std::optional<std::size_t> at = snapshot.tracks[index].midiSource;
    for (std::size_t step = 0; at && step < snapshot.tracks.size(); ++step) {
        if (*at == index) {
            return true;
        }
        at = snapshot.tracks[*at].midiSource;
    }
    return false;
}

std::size_t midiDepth(const Snapshot& snapshot, std::size_t index) {
    std::size_t depth = 0;
    for (auto at = snapshot.tracks[index].midiSource; at; at = snapshot.tracks[*at].midiSource) {
        ++depth;
    }
    return depth;
}

// Resolves each track's MIDI source to the index of a track with an instrument and orders the instruments so each
// plays after its source. A loop of MIDI routes loses them, so no instrument waits on itself.
void connectMidiSources(Snapshot& snapshot, const Session& session, std::vector<SessionError>& errors) {
    for (std::size_t index = 0; index < session.tracks.size(); ++index) {
        const std::string& source = session.tracks[index].midiInTrack;
        if (source.empty()) {
            continue;
        }
        const auto found = std::ranges::find(snapshot.tracks, source, &TrackState::id);
        if (found == snapshot.tracks.end() || !found->instrument) {
            errors.push_back(
                {snapshot.tracks[index].id, "its MIDI input track plays no instrument, so it gets no MIDI"});
            continue;
        }
        snapshot.tracks[index].midiSource = static_cast<std::size_t>(found - snapshot.tracks.begin());
    }
    const auto looped = std::views::iota(std::size_t{0}, snapshot.tracks.size()) |
                        std::views::filter([&](std::size_t index) { return inMidiLoop(snapshot, index); }) |
                        std::ranges::to<std::vector>();
    for (const std::size_t index : looped) {
        snapshot.tracks[index].midiSource.reset();
        errors.push_back({snapshot.tracks[index].id, "its MIDI input feeds back into it, so it gets no MIDI"});
    }
    snapshot.instrumentOrder =
        std::views::iota(std::size_t{0}, snapshot.tracks.size()) |
        std::views::filter([&](std::size_t index) { return snapshot.tracks[index].instrument != nullptr; }) |
        std::ranges::to<std::vector>();
    std::ranges::stable_sort(snapshot.instrumentOrder, {},
                             [&](std::size_t index) { return midiDepth(snapshot, index); });
}

std::optional<std::size_t> sendIndex(const Session& session, const TrackState& state, const std::string& bus) {
    const auto busIndex =
        static_cast<std::size_t>(std::ranges::find(session.tracks, bus, &Track::id) - session.tracks.begin());
    const auto send = std::ranges::find(state.sends, busIndex, &SendState::bus);
    return send == state.sends.end() ? std::nullopt
                                     : std::optional{static_cast<std::size_t>(send - state.sends.begin())};
}

// Runs after connectBuses, so a send lane finds its send, and a lane whose send went silent plays nothing.
void addLanes(Snapshot& snapshot, const Session& session) {
    for (std::size_t index = 0; index < session.tracks.size(); ++index) {
        TrackState& state = snapshot.tracks[index];
        if (state.mix.empty()) {
            continue;
        }
        for (const AutomationLane& lane : session.tracks[index].automation) {
            if (lane.points.empty()) {
                continue;
            }
            LaneState added{.target = lane.target, .send = 0, .insert = 0, .param = 0, .points = {}};
            if (lane.target == AutomationTarget::send) {
                const auto send = sendIndex(session, state, lane.bus);
                if (!send) {
                    continue;
                }
                added.send = *send;
            }
            if (lane.target == AutomationTarget::insert) {
                const auto& inserts = session.tracks[index].inserts;
                const auto insert = std::ranges::find(inserts, lane.insert, &Insert::id);
                if (insert == inserts.end() || (!insert->plugin && lane.param >= kMaxEffectParams)) {
                    continue;
                }
                added.insert = static_cast<std::size_t>(insert - inserts.begin());
                added.param = lane.param;
            }
            added.points = lane.points | std::views::transform([&](const AutomationPoint& point) {
                               return LanePoint{.frame = session.tempo.ticksToFrames(point.tick, snapshot.sampleRate),
                                                .value = point.value,
                                                .curve = point.curve};
                           }) |
                           std::ranges::to<std::vector>();
            state.lanes.push_back(std::move(added));
        }
    }
}

// Data entry and RPN or NRPN selects only make sense in their order, and mode messages act at once, so we leave them
// out.
bool chasedController(std::uint8_t controller) {
    constexpr std::uint8_t kDataEntry = 6;
    constexpr std::uint8_t kDataEntryLsb = 38;
    constexpr std::uint8_t kFirstParameterCc = 96;
    constexpr std::uint8_t kLastParameterCc = 101;
    return controller != kDataEntry && controller != kDataEntryLsb &&
           (controller < kFirstParameterCc || controller > kLastParameterCc) && controller < core::midi::kAllSoundOffCc;
}

bool chases(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || !core::midi::isChannelMessage(bytes[0])) {
        return false;
    }
    switch (bytes[0] & 0xF0) {
        case core::midi::kControlChange:
            return bytes.size() == 3 && chasedController(bytes[1]);
        case core::midi::kProgramChange:
            return bytes.size() == 2;
        case core::midi::kPitchBend:
            return bytes.size() == 3;
        default:
            return false;
    }
}

void addOutput(Snapshot& snapshot, const std::shared_ptr<MidiOutputPort>& port) {
    if (port && std::ranges::find(snapshot.outputs, port) == snapshot.outputs.end()) {
        snapshot.outputs.push_back(port);
    }
}

void collectOutputs(Snapshot& snapshot) {
    for (const TrackState& track : snapshot.tracks) {
        addOutput(snapshot, track.midiOut);
    }
    for (const auto& port : snapshot.clockPorts) {
        addOutput(snapshot, port);
    }
    addOutput(snapshot, snapshot.metronome.midiOut);
}

void addCycle(Snapshot& snapshot, const Session& session) {
    if (session.cycle.end <= session.cycle.start) {
        return;
    }
    snapshot.cycleStart = session.tempo.ticksToFrames(session.cycle.start, snapshot.sampleRate);
    snapshot.cycleEnd = session.tempo.ticksToFrames(session.cycle.end, snapshot.sampleRate);
    for (const PortMessage& message : session.cycle.restart) {
        if (!message.trackId.empty()) {
            const auto track = std::ranges::find(snapshot.tracks, message.trackId, &TrackState::id);
            if (track != snapshot.tracks.end() && track->instrument && !message.bytes.empty()) {
                snapshot.cycleRestart.push_back(
                    {.port = nullptr, .instrument = track->instrument, .bytes = message.bytes});
            }
            continue;
        }
        const auto port = std::ranges::find(snapshot.outputs, message.portId,
                                            [](const auto& output) -> const std::string& { return output->id(); });
        if (port != snapshot.outputs.end() && !message.bytes.empty()) {
            snapshot.cycleRestart.push_back({.port = *port, .instrument = nullptr, .bytes = message.bytes});
        }
    }
}

}  // namespace

SnapshotBuilder::SnapshotBuilder(MidiBackend& midi, TakeInbox& inbox, PollingSet<ClipStream>& reader,
                                 PollingSet<TakeRecorder>& writer, PollingSet<MidiOutputPort>& sender)
    : midi_(midi), inbox_(inbox), reader_(reader), writer_(writer), sender_(sender) {}

BuiltSnapshot SnapshotBuilder::build(const Session& session, const StreamLayout& layout, Frames captureOffset) {
    BuiltSnapshot built{std::make_unique<Snapshot>(), {}};
    Snapshot& snapshot = *built.snapshot;
    snapshot.tempo = session.tempo;
    snapshot.sampleRate = layout.sampleRate;
    snapshot.panLaw = session.panLaw;
    snapshot.sendStart = session.sendStart;
    for (const std::string& id : session.clockPorts) {
        snapshot.clockPorts.push_back(outputPort(id));
    }
    for (const Track& track : session.tracks) {
        TrackState state;
        state.id = track.id;
        state.kind = track.kind;
        state.armed = track.armed;
        state.mute = track.mute;
        state.solo = track.solo;
        state.monitor = track.monitor;
        state.gain = track.gain;
        state.trim = track.trim;
        state.trimFrom = track.trim;
        state.pan = track.pan;
        state.inputs = channelIndices("track " + track.id, track.audioIn, layout, true);
        if (track.outputBus.empty()) {
            state.outputs = channelIndices("track " + track.id, track.audioOut, layout, false);
        }
        state.captures = channelIndices("track " + track.id, track.captureOut, layout, false);
        state.inputPointers.resize(state.inputs.size());
        state.inChannel = track.midiIn.channel;
        state.outChannel = track.midiOut.channel;
        state.thru = track.midiThru;
        state.claimsSysex = track.claimsSysex;
        if (!track.midiIn.portId.empty()) {
            state.midiIn = inputPort(track.midiIn.portId);
            if (std::ranges::find(snapshot.inputs, state.midiIn) == snapshot.inputs.end()) {
                snapshot.inputs.push_back(state.midiIn);
            }
        }
        if (!track.midiOut.portId.empty()) {
            state.midiOut = outputPort(track.midiOut.portId);
        }
        if (const std::size_t channels = mixChannels(track, state); channels > 0) {
            state.outputMeter = meter(outputMeters_, track.id, channels);
            state.mix.assign(channels, std::vector<float>(static_cast<std::size_t>(layout.maxFrames)));
            state.outputGains = std::views::iota(std::size_t{0}, channels) |
                                std::views::transform(
                                    [&](std::size_t channel) { return outputGain(state, channel, snapshot.panLaw); }) |
                                std::ranges::to<std::vector>();
            state.mixPointers = state.mix |
                                std::views::transform([](std::vector<float>& channel) { return channel.data(); }) |
                                std::ranges::to<std::vector>();
            addInserts(state, track, layout, built.errors);
        }
        if (track.instrument && track.instrument->activate(layout.sampleRate, layout.maxFrames)) {
            state.instrument = track.instrument;
        } else if (track.instrument) {
            built.errors.push_back({track.id, track.instrument->name() + " cannot play at " +
                                                  std::to_string(std::lround(layout.sampleRate)) + " Hz"});
        }
        addClips(state, track, session, layout, built.errors);
        state.midi = midiSchedule(track, session.tempo, layout.sampleRate);
        addRecorder(state, track, session, layout, captureOffset);
        snapshot.tracks.push_back(std::move(state));
    }
    connectBuses(snapshot, session, built.errors);
    connectMidiSources(snapshot, session, built.errors);
    addLanes(snapshot, session);
    updateSoloPaths(snapshot);
    addMetronome(snapshot, session, layout, built.errors);
    collectOutputs(snapshot);
    addCycle(snapshot, session);
    snapshot.chaseMidi = session.chaseMidi;
    snapshot.clockWhileStopped = session.clockWhileStopped;
    return built;
}

void SnapshotBuilder::addInserts(TrackState& state, const Track& track, const StreamLayout& layout,
                                 std::vector<SessionError>& errors) {
    for (const Insert& insert : track.inserts) {
        if (insert.plugin) {
            if (insert.plugin->activate(layout.sampleRate, layout.maxFrames)) {
                state.inserts.push_back(
                    {.effect = nullptr, .plugin = insert.plugin, .params = {}, .bypass = insert.bypass});
            } else {
                errors.push_back({track.id, insert.plugin->name() + " cannot play at " +
                                                std::to_string(std::lround(layout.sampleRate)) + " Hz"});
            }
            continue;
        }
        const std::string key = track.id + "/" + insert.id + "/" + std::to_string(static_cast<int>(insert.type)) + "/" +
                                std::to_string(state.mix.size()) + "/" + std::to_string(layout.sampleRate) + "/" +
                                std::to_string(layout.maxFrames);
        std::shared_ptr<Effect> effect = cached(effects_, key, [&] {
            return std::make_shared<Effect>(insert.type, state.mix.size(), layout.sampleRate, layout.maxFrames);
        });
        if (effectSpec(insert.type).reportsReduction) {
            meteredInserts_[insert.id] = effect;
        }
        state.inserts.push_back(
            {.effect = std::move(effect), .plugin = nullptr, .params = insert.params, .bypass = insert.bypass});
    }
}

void SnapshotBuilder::addMetronome(Snapshot& snapshot, const Session& session, const StreamLayout& layout,
                                   std::vector<SessionError>& errors) {
    const Metronome& settings = session.metronome;
    MetronomeState& metronome = snapshot.metronome;
    metronome.settings = settings;
    metronome.countInFrames = session.tempo.ticksToFrames(
        std::clamp(settings.countInBars, 0, kMaxCountInBars) * session.tempo.ticksPerBar(), layout.sampleRate);
    metronome.outputs = std::views::iota(0, std::min(layout.outputChannels, 2)) | std::ranges::to<std::vector>();
    if (!settings.audioOut.deviceId.empty()) {
        try {
            metronome.outputs = channelIndices(kMetronomeId, settings.audioOut, layout, false);
        } catch (const std::invalid_argument& error) {
            errors.push_back({kMetronomeId, std::string{error.what()} + ", so it plays on the main output"});
        }
    }
    const bool midi = settings.mode == MetronomeMode::midiClick || settings.mode == MetronomeMode::midiGroove;
    if (midi && !settings.midiOut.portId.empty()) {
        metronome.midiOut = outputPort(settings.midiOut.portId);
    }
    metronome.sounds = drumSounds(settings, layout.sampleRate, errors);
}

std::shared_ptr<const DrumSounds> SnapshotBuilder::drumSounds(const Metronome& metronome, double sampleRate,
                                                              std::vector<SessionError>& errors) {
    const bool samples = metronome.mode == MetronomeMode::sample;
    const std::string key = std::to_string(sampleRate) + "/" +
                            (samples ? metronome.accentSample.string() + "/" + metronome.beatSample.string() : "");
    if (!sounds_ || key != soundsKey_) {
        DrumSounds sounds = synthesizeDrums(sampleRate);
        soundErrors_.clear();
        if (samples) {
            const auto load = [&](const std::filesystem::path& path, DrumVoice voice) {
                if (path.empty()) {
                    return;
                }
                auto loaded = loadOneShot(path, sampleRate);
                if (loaded) {
                    sounds.voices[static_cast<std::size_t>(voice)] = std::move(*loaded);
                } else {
                    soundErrors_.push_back({kMetronomeId, loaded.error() + ", so the click plays instead"});
                }
            };
            load(metronome.accentSample, DrumVoice::accent);
            load(metronome.beatSample, DrumVoice::beat);
        }
        sounds_ = std::make_shared<const DrumSounds>(std::move(sounds));
        soundsKey_ = key;
    }
    errors.insert(errors.end(), soundErrors_.begin(), soundErrors_.end());
    return sounds_;
}

void SnapshotBuilder::addClips(TrackState& state, const Track& track, const Session& session,
                               const StreamLayout& layout, std::vector<SessionError>& errors) {
    const core::TempoMap& tempo = session.tempo;
    const bool cycling = session.cycle.end > session.cycle.start;
    const Frames cycleStart = tempo.ticksToFrames(session.cycle.start, layout.sampleRate);
    for (std::size_t index = 0; index < track.audioClips.size(); ++index) {
        const AudioClip& clip = track.audioClips[index];
        const Frames start = tempo.ticksToFrames(clip.start, layout.sampleRate);
        const Frames length = tempo.ticksToFrames(clip.start + clip.length, layout.sampleRate) - start;
        const std::string key = track.id + "/" + std::to_string(index) + "/" + clip.path.string() + "/" +
                                std::to_string(start) + "/" + std::to_string(length) + "/" +
                                std::to_string(clip.sourceOffset);
        std::shared_ptr<ClipStream> stream = clips_[key].lock();
        if (!stream) {
            auto opened = openClipFile(clip.path, layout.sampleRate);
            if (!opened) {
                errors.push_back({track.id, std::move(opened.error())});
                continue;
            }
            stream = std::make_shared<ClipStream>(std::move(*opened), clip.sourceOffset, length, layout.sampleRate,
                                                  layout.maxFrames);
            clips_[key] = stream;
            reader_.add(stream);
        }
        ClipPlayback playback{.stream = std::move(stream),
                              .start = start,
                              .length = length,
                              .gain = clip.gain,
                              .fadeIn = tempo.ticksToFrames(clip.fadeIn, layout.sampleRate),
                              .fadeOut = tempo.ticksToFrames(clip.fadeOut, layout.sampleRate),
                              .fadeInCurve = clip.fadeInCurve,
                              .fadeOutCurve = clip.fadeOutCurve,
                              .preload = nullptr};
        if (cycling) {
            playback.preload = preload(key, clip, playback, cycleStart, layout.sampleRate);
        }
        state.clips.push_back(std::move(playback));
    }
}

std::shared_ptr<const Preload> SnapshotBuilder::preload(const std::string& key, const AudioClip& clip,
                                                        const ClipPlayback& playback, Frames cycleStart,
                                                        double sampleRate) {
    const Frames from = std::max(cycleStart, playback.start);
    const Frames to = std::min(cycleStart + static_cast<Frames>(std::llround(kPreloadSeconds * sampleRate)),
                               playback.start + playback.length);
    if (from >= to) {
        return nullptr;
    }
    const std::string preloadKey = key + "/" + std::to_string(from) + "/" + std::to_string(to);
    if (auto alive = preloads_[preloadKey].lock()) {
        return alive;
    }
    auto read = preloadClip(clip.path, sampleRate, clip.sourceOffset, from - playback.start, to - from);
    if (!read) {
        return nullptr;
    }
    auto made = std::make_shared<const Preload>(*std::move(read));
    preloads_[preloadKey] = made;
    return made;
}

std::shared_ptr<const MidiSchedule> SnapshotBuilder::midiSchedule(const Track& track, const core::TempoMap& tempo,
                                                                  double sampleRate) {
    CachedSchedule& cache = schedules_[track.id];
    const auto clips = track.midiClips.shared();
    if (auto alive = cache.schedule.lock(); alive && cache.clips == clips && cache.tempo == tempo &&
                                            cache.sampleRate == sampleRate && cache.kind == track.kind) {
        return alive;
    }
    auto schedule = std::make_shared<MidiSchedule>();
    for (const MidiClip& clip : track.midiClips) {
        for (const MidiEvent& event : clip.events) {
            if (event.bytes.empty()) {
                continue;
            }
            schedule->events.push_back(
                {tempo.ticksToFrames(clip.start + event.tick, sampleRate), schedule->bytes.size(), event.bytes.size()});
            schedule->bytes.insert(schedule->bytes.end(), event.bytes.begin(), event.bytes.end());
        }
    }
    std::ranges::stable_sort(schedule->events, {}, &ScheduledMidi::frame);
    if (track.kind == TrackKind::midi) {
        schedule->chaseEvents = schedule->events | std::views::filter([&](const ScheduledMidi& event) {
                                    return chases(std::span(schedule->bytes).subspan(event.offset, event.size));
                                }) |
                                std::ranges::to<std::vector>();
    }
    cache = {.clips = clips, .tempo = tempo, .sampleRate = sampleRate, .kind = track.kind, .schedule = schedule};
    return schedule;
}

void SnapshotBuilder::addRecorder(TrackState& state, const Track& track, const Session& session,
                                  const StreamLayout& layout, Frames captureOffset) {
    const bool canRecord =
        track.kind == TrackKind::audio ? !state.inputs.empty() : state.midiIn != nullptr || !track.midiInTrack.empty();
    if (!canRecord) {
        return;
    }
    if (track.kind == TrackKind::audio) {
        state.inputMeter = meter(inputMeters_, track.id, state.inputs.size());
    }
    state.recordDelay = std::llround(track.recordDelayMs * layout.sampleRate / kMillisecondsPerSecond);
    const int channels = static_cast<int>(state.inputs.size());
    const std::string key = track.id + "/" + std::to_string(static_cast<int>(track.kind)) + "/" +
                            std::to_string(channels) + "/" + std::to_string(captureOffset) + "/" +
                            session.takeFolder.string();
    state.recorder = cached(recorders_, key, [&] {
        auto made = std::make_shared<TakeRecorder>(track.id, track.kind, channels, layout.sampleRate, layout.maxFrames,
                                                   captureOffset, session.takeFolder, inbox_);
        writer_.add(made);
        return made;
    });
}

std::shared_ptr<MidiInputPort> SnapshotBuilder::inputPort(const std::string& id) {
    return cached(inputs_, id, [&] { return std::make_shared<MidiInputPort>(midi_, id); });
}

std::shared_ptr<MidiOutputPort> SnapshotBuilder::outputPort(const std::string& id) {
    return cached(outputs_, id, [&] {
        auto port = std::make_shared<MidiOutputPort>(midi_, id);
        sender_.add(port);
        return port;
    });
}

void SnapshotBuilder::prune() {
    const auto expired = [](const auto& entry) { return entry.second.expired(); };
    std::erase_if(clips_, expired);
    std::erase_if(preloads_, expired);
    std::erase_if(schedules_, [](const auto& entry) { return entry.second.schedule.expired(); });
    std::erase_if(effects_, expired);
    std::erase_if(meteredInserts_, expired);
    std::erase_if(recorders_, expired);
    std::erase_if(inputMeters_, expired);
    std::erase_if(outputMeters_, expired);
    std::erase_if(inputs_, expired);
    std::erase_if(outputs_, expired);
}

bool SnapshotBuilder::clipsBuffered() const {
    return std::ranges::all_of(clips_, [](const auto& entry) {
        const auto stream = entry.second.lock();
        return !stream || stream->buffered();
    });
}

bool SnapshotBuilder::midiFlushed() const {
    return std::ranges::all_of(outputs_, [](const auto& entry) {
        const auto port = entry.second.lock();
        return !port || port->flushed();
    });
}

}  // namespace emerick::engine
