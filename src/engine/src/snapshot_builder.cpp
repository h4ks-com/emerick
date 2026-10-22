#include "snapshot_builder.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace emerick::engine {

namespace {

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
        state.pan = track.pan;
        state.inputs = channelIndices("track " + track.id, track.audioIn, layout, true);
        state.outputs = channelIndices("track " + track.id, track.audioOut, layout, false);
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
        if (!state.outputs.empty()) {
            state.outputMeter = meter(outputMeters_, track.id, state.outputs.size());
            state.mix.assign(state.outputs.size(), std::vector<float>(static_cast<std::size_t>(layout.maxFrames)));
            state.outputGains =
                std::views::iota(std::size_t{0}, state.outputs.size()) |
                std::views::transform([&](std::size_t output) { return outputGain(state, output, snapshot.panLaw); }) |
                std::ranges::to<std::vector>();
        }
        addClips(state, track, session.tempo, layout, built.errors);
        addMidiEvents(state, track, session.tempo, layout.sampleRate);
        addRecorder(state, track, session, layout, captureOffset);
        snapshot.tracks.push_back(std::move(state));
    }
    addMetronome(snapshot, session, layout, built.errors);
    snapshot.tailOutputs.resize(static_cast<std::size_t>(layout.outputChannels));
    return built;
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

void SnapshotBuilder::addClips(TrackState& state, const Track& track, const core::TempoMap& tempo,
                               const StreamLayout& layout, std::vector<SessionError>& errors) {
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
        state.clips.push_back(
            {std::move(stream), start, length, clip.gain, tempo.ticksToFrames(clip.fadeIn, layout.sampleRate),
             tempo.ticksToFrames(clip.fadeOut, layout.sampleRate), clip.fadeInCurve, clip.fadeOutCurve});
    }
}

void SnapshotBuilder::addMidiEvents(TrackState& state, const Track& track, const core::TempoMap& tempo,
                                    double sampleRate) {
    for (const MidiClip& clip : track.midiClips) {
        for (const MidiEvent& event : clip.events) {
            if (event.bytes.empty()) {
                continue;
            }
            state.midiEvents.push_back(
                {tempo.ticksToFrames(clip.start + event.tick, sampleRate), state.midiBytes.size(), event.bytes.size()});
            state.midiBytes.insert(state.midiBytes.end(), event.bytes.begin(), event.bytes.end());
        }
    }
    std::ranges::stable_sort(state.midiEvents, {}, &ScheduledMidi::frame);
}

void SnapshotBuilder::addRecorder(TrackState& state, const Track& track, const Session& session,
                                  const StreamLayout& layout, Frames captureOffset) {
    const bool canRecord = track.kind == TrackKind::audio ? !state.inputs.empty() : state.midiIn != nullptr;
    if (!canRecord) {
        return;
    }
    if (track.kind == TrackKind::audio) {
        state.inputMeter = meter(inputMeters_, track.id, state.inputs.size());
    }
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
