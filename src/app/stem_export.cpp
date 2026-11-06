#include "emerick/app/stem_export.hpp"

#include <sndfile.h>

#include <QCoreApplication>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#endif

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "emerick/app/audio_file.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/track_rows.hpp"
#include "emerick/engine/audio_analysis.hpp"
#include "emerick/engine/engine.hpp"
#include "emerick/engine/sound_file.hpp"
#include "emerick/model/clip_edit.hpp"
#include "emerick/model/document.hpp"
#include "sampler_params.hpp"

namespace emerick::app {

namespace {

constexpr engine::Frames kBlock = 512;
constexpr engine::Frames kChunk = 24000;
constexpr int kSettleBlocks = 16;
constexpr auto kReadAheadTimeout = std::chrono::seconds{5};
constexpr const char* kRenderDevice = OfflineRender::kDevice;

using RenderInstruments = std::vector<std::shared_ptr<engine::Plugin>>;

constexpr std::chrono::seconds kSamplerLoadTimeout{120};
constexpr const char* kStemsPrefix = "emerick-stems-";

std::string fileStem(const std::string& name) {
    std::string stem = name.empty() ? std::string{"track"} : name;
    std::ranges::replace_if(stem, [](char c) { return std::string_view{"/\\:*?\"<>|"}.contains(c); }, '_');
    return stem;
}

bool hasInstrument(const model::Document& document, const std::string& id) {
    const model::Track* track = model::findTrack(document, model::TrackId{id});
    return track != nullptr && track->instrument.has_value();
}

bool playsAudio(const engine::Track& track, const model::Document& document) {
    return track.kind != engine::TrackKind::midi || hasInstrument(document, track.id);
}

// A track that sends MIDI on to another track's instrument, as an Arranger does, gets no stem of its own.
bool feedsMidi(const engine::Session& session, const engine::Track& track) {
    return std::ranges::contains(session.tracks, track.id, &engine::Track::midiInTrack);
}

std::vector<engine::Track> stemTracks(const engine::Session& session, const model::Document& document) {
    return session.tracks | std::views::filter([&](const engine::Track& track) {
               return playsAudio(track, document) && !feedsMidi(session, track);
           }) |
           std::ranges::to<std::vector>();
}

engine::AudioRoute renderRoute() { return {kRenderDevice, {1, 2}}; }

engine::AudioRoute mixRoute(const engine::Track& track, const model::Document& document) {
    return playsAudio(track, document) ? renderRoute() : engine::AudioRoute{};
}

engine::Track audible(engine::Track track) {
    track.mute = false;
    track.solo = false;
    return track;
}

engine::Session renderSession(const engine::Session& session, std::vector<engine::Track> tracks) {
    return {.tempo = session.tempo,
            .tracks = std::move(tracks),
            .clockPorts = {},
            .takeFolder = {},
            .metronome = {},
            .panLaw = session.panLaw,
            .taps = {}};
}

// A bus stem plays the bus alone with every track and bus feeding it, so only the bus reaches the render output.
std::vector<engine::Track> busStemTracks(const std::vector<engine::Track>& tracks, const engine::Track& bus) {
    return tracks | std::views::transform([&](const engine::Track& source) {
               engine::Track track = audible(source);
               track.audioOut = track.id == bus.id ? renderRoute() : engine::AudioRoute{};
               if (track.id == bus.id) {
                   track.outputBus.clear();
               }
               return track;
           }) |
           std::ranges::to<std::vector>();
}

engine::Track dry(const engine::Track& source, engine::AudioRoute out) {
    engine::Track track = audible(source);
    track.audioOut = std::move(out);
    track.outputBus.clear();
    track.sends.clear();
    return track;
}

// A track stem plays the track dry and straight to the render output, with the tracks that send it MIDI playing
// into no output.
std::vector<engine::Track> trackStemTracks(const std::vector<engine::Track>& tracks, const engine::Track& track) {
    std::vector<engine::Track> stem;
    for (std::string source = track.midiInTrack; !source.empty();) {
        const auto found = std::ranges::find(tracks, source, &engine::Track::id);
        if (found == tracks.end() || found->id == track.id || std::ranges::contains(stem, source, &engine::Track::id)) {
            break;
        }
        stem.push_back(dry(*found, {}));
        source = found->midiInTrack;
    }
    stem.push_back(dry(track, renderRoute()));
    return stem;
}

engine::Session stemSession(const engine::Session& session, const engine::Track& track) {
    return renderSession(session, track.kind == engine::TrackKind::bus ? busStemTracks(session.tracks, track)
                                                                       : trackStemTracks(session.tracks, track));
}

engine::Session mixSession(const engine::Session& session, const model::Document& document) {
    std::vector<engine::Track> tracks = session.tracks;
    for (engine::Track& track : tracks) {
        track.audioOut = mixRoute(track, document);
    }
    return renderSession(session, std::move(tracks));
}

// Every track plays unmuted into its buses and sends as in a bus stem, so each listed track's own output sounds as its
// stem. With mixOf, output channels 1 and 2 also carry the mix of every track that plays audio in that document, and
// the listed tracks move up two channels.
engine::Session captureSession(const engine::Session& session, const std::vector<std::string>& trackIds,
                               const model::Document* mixOf = nullptr) {
    std::vector<engine::Track> tracks =
        session.tracks | std::views::transform(audible) | std::ranges::to<std::vector>();
    const int offset = mixOf != nullptr ? 2 : 0;
    for (engine::Track& track : tracks) {
        track.audioOut = mixOf != nullptr ? mixRoute(track, *mixOf) : engine::AudioRoute{};
        if (const auto found = std::ranges::find(trackIds, track.id); found != trackIds.end()) {
            const auto first = static_cast<int>(2 * (found - trackIds.begin())) + 1 + offset;
            track.captureOut = {kRenderDevice, {first, first + 1}};
        }
    }
    return renderSession(session, std::move(tracks));
}

// The buses a track feeds, in the order the engine feeds them: its sends, then its output bus.
std::vector<std::size_t> busesFed(const std::vector<engine::Track>& tracks, std::size_t index) {
    std::vector<std::size_t> buses;
    const auto add = [&](const std::string& id) {
        const auto found = std::ranges::find(tracks, id, &engine::Track::id);
        const auto bus = static_cast<std::size_t>(found - tracks.begin());
        if (found != tracks.end() && found->kind == engine::TrackKind::bus && bus != index) {
            buses.push_back(bus);
        }
    };
    for (const engine::Send& send : tracks[index].sends) {
        add(send.bus);
    }
    if (!tracks[index].outputBus.empty()) {
        add(tracks[index].outputBus);
    }
    return buses;
}

// How a session's tracks feed each other, which the engine mixes in order: every track before the buses it feeds.
struct MixGraph {
    std::vector<std::vector<std::size_t>> fed;
    std::vector<std::size_t> order;
    std::vector<std::size_t> position;
    // 0 for a track nothing feeds, else one more than the highest track that feeds it.
    std::vector<std::size_t> level;
};

// The engine's mix order and the levels, or nothing when buses feed each other in a loop.
std::optional<MixGraph> mixGraph(const std::vector<engine::Track>& tracks) {
    const std::size_t count = tracks.size();
    MixGraph graph{
        .fed = {}, .order = {}, .position = std::vector<std::size_t>(count), .level = std::vector<std::size_t>(count)};
    graph.fed = std::views::iota(std::size_t{0}, count) |
                std::views::transform([&](std::size_t index) { return busesFed(tracks, index); }) |
                std::ranges::to<std::vector>();
    std::vector<std::size_t> sources(count);
    for (const std::vector<std::size_t>& buses : graph.fed) {
        for (const std::size_t bus : buses) {
            ++sources[bus];
        }
    }
    graph.order = std::views::iota(std::size_t{0}, count) |
                  std::views::filter([&](std::size_t index) { return sources[index] == 0; }) |
                  std::ranges::to<std::vector>();
    for (std::size_t position = 0; position < graph.order.size(); ++position) {
        const std::size_t index = graph.order[position];
        for (const std::size_t bus : graph.fed[index]) {
            graph.level[bus] = std::max(graph.level[bus], graph.level[index] + 1);
            if (--sources[bus] == 0) {
                graph.order.push_back(bus);
            }
        }
    }
    if (graph.order.size() != count) {
        return std::nullopt;
    }
    for (std::size_t position = 0; position < count; ++position) {
        graph.position[graph.order[position]] = position;
    }
    return graph;
}

// The track plays its signal before its fader from the render's inputs from input on, or silence without one, in place
// of its instrument, its clips and its inserts, so what reaches its fader stays the same and costs nothing to render.
engine::Track frozen(engine::Track track, std::optional<int> input) {
    track.kind = engine::TrackKind::audio;
    track.instrument = nullptr;
    track.midiClips = {};
    track.midiInTrack.clear();
    track.inserts.clear();
    track.trim = 1.0F;
    std::erase_if(track.automation,
                  [](const engine::AutomationLane& lane) { return lane.target == engine::AutomationTarget::insert; });
    track.audioClips.clear();
    track.audioIn = input ? engine::AudioRoute{kRenderDevice, {*input, *input + 1}} : engine::AudioRoute{};
    track.audioOut = {};
    track.captureOut = {};
    track.chainOut = {};
    return track;
}

// The document a render of frozen tracks attaches plugins from, with no instrument or plugin insert on them.
model::Document frozenDocument(const model::Document& document, const std::vector<std::string>& frozenIds) {
    model::Document copy = document;
    for (model::Track& track : copy.tracks) {
        if (std::ranges::contains(frozenIds, track.id.value)) {
            track.instrument.reset();
            track.inserts.clear();
        }
    }
    return copy;
}

// The chunks of one track's signal before its fader, which a render pushes in order and each render it feeds reads
// in order. A pusher waits while its readers lag far behind, so the stream holds a few chunks at a time.
class StemStream {
public:
    explicit StemStream(std::size_t readers) : readers_(readers) {}

    // Waits while the readers lag kAhead chunks behind; a stream its readers gave up on drops the chunk.
    void push(std::vector<std::vector<float>> chunk) {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [this] { return abandoned_ || chunks_.size() - freed_ < kAhead; });
        if (abandoned_) {
            return;
        }
        chunks_.push_back({.channels = std::make_shared<const Channels>(std::move(chunk)), .unread = readers_});
        changed_.notify_all();
    }

    // Waits for chunk index. Throws AppError once the render that pushes it failed.
    std::shared_ptr<const std::vector<std::vector<float>>> read(std::size_t index) {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return failed_ || index < chunks_.size(); });
        if (index >= chunks_.size()) {
            throw AppError("cannot render: a track or bus it plays failed to render");
        }
        Entry& entry = chunks_[index];
        auto channels = entry.channels;
        if (--entry.unread == 0) {
            entry.channels.reset();
            while (freed_ < chunks_.size() && !chunks_[freed_].channels) {
                ++freed_;
            }
            changed_.notify_all();
        }
        return channels;
    }

    // The render that pushes the stream failed, so its readers stop.
    void fail() {
        const std::scoped_lock lock(mutex_);
        failed_ = true;
        changed_.notify_all();
    }

    // A render that reads the stream failed, so its pusher stops waiting for it and its other readers stop too.
    void abandon() {
        const std::scoped_lock lock(mutex_);
        abandoned_ = true;
        failed_ = true;
        changed_.notify_all();
    }

private:
    using Channels = std::vector<std::vector<float>>;
    static constexpr std::size_t kAhead = 16;

    struct Entry {
        std::shared_ptr<const Channels> channels;
        std::size_t unread = 0;
    };

    std::size_t readers_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Entry> chunks_;
    std::size_t freed_ = 0;
    bool failed_ = false;
    bool abandoned_ = false;
};

// Stops the renders a failed render feeds and frees the renders that feed it, unless the render played.
class FailedStage {
public:
    FailedStage(std::span<const std::size_t> writes, std::span<const std::size_t> reads,
                std::vector<std::unique_ptr<StemStream>>& streams)
        : writes_(writes), reads_(reads), streams_(&streams) {}
    FailedStage(const FailedStage&) = delete;
    FailedStage& operator=(const FailedStage&) = delete;
    FailedStage(FailedStage&&) = delete;
    FailedStage& operator=(FailedStage&&) = delete;
    // NOLINTNEXTLINE(bugprone-exception-escape): a mutex that cannot lock ends the process.
    ~FailedStage() {
        if (played_) {
            return;
        }
        for (const std::size_t index : writes_) {
            (*streams_)[index]->fail();
        }
        for (const std::size_t index : reads_) {
            (*streams_)[index]->abandon();
        }
    }

    void played() { played_ = true; }

private:
    std::span<const std::size_t> writes_;
    std::span<const std::size_t> reads_;
    std::vector<std::unique_ptr<StemStream>>* streams_;
    bool played_ = false;
};

// Runs renders on worker threads while the calling thread, their instruments' main thread, runs the idle work the
// instruments ask for between blocks, so each render plays exactly as it would alone on the main thread.
class MainThreadWork {
public:
    // Worker thread: has the main thread run the render's idle work and waits until it did.
    void idle(OfflineRender& render) {
        Ask ask{.render = &render, .done = false};
        std::unique_lock lock(mutex_);
        asks_.push_back(&ask);
        changed_.notify_all();
        changed_.wait(lock, [&ask] { return ask.done; });
    }

    // Main thread: runs every job on its own thread, since a job can wait on any other, serving idle work until
    // they all end, and rethrows the first exception a job threw.
    void run(const std::vector<std::function<void()>>& jobs) {
        running_ = jobs.size();
        std::vector<std::future<void>> workers;
        workers.reserve(jobs.size());
        for (const std::function<void()>& job : jobs) {
            workers.push_back(std::async(std::launch::async, [this, &job] {
                const Finished finished{*this};
                job();
            }));
        }
        std::unique_lock lock(mutex_);
        for (;;) {
            changed_.wait(lock, [this] { return !asks_.empty() || running_ == 0; });
            if (asks_.empty()) {
                break;
            }
            const std::vector<Ask*> served = std::exchange(asks_, {});
            lock.unlock();
            for (const Ask* ask : served) {
                ask->render->idleInstruments();
            }
            lock.lock();
            for (Ask* ask : served) {
                ask->done = true;
            }
            changed_.notify_all();
        }
        lock.unlock();
        for (std::future<void>& worker : workers) {
            worker.get();
        }
    }

private:
    struct Ask {
        OfflineRender* render;
        bool done;
    };

    // Counts a worker out when it ends, also by an exception, so the main thread stops serving once all have ended.
    struct Finished {
        explicit Finished(MainThreadWork& owner) : work(owner) {}
        Finished(const Finished&) = delete;
        Finished& operator=(const Finished&) = delete;
        Finished(Finished&&) = delete;
        Finished& operator=(Finished&&) = delete;
        // NOLINTNEXTLINE(bugprone-exception-escape): a mutex that cannot lock ends the process.
        ~Finished() {
            const std::scoped_lock lock(work.mutex_);
            --work.running_;
            work.changed_.notify_all();
        }

        MainThreadWork& work;
    };

    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<Ask*> asks_;
    std::size_t running_ = 0;
};

// Emerick Sampler loads its SFZ file on a thread of its own, and swaps it in on the main thread before it activates.
void waitForSampler(const model::Track& track, engine::Plugin& plugin) {
    const auto deadline = std::chrono::steady_clock::now() + kSamplerLoadTimeout;
    while (plugin.paramValue(sampler::kLoadingParam) == 1.0) {
        if (std::chrono::steady_clock::now() > deadline) {
            throw AppError(std::format("cannot render track {}: its SFZ instrument still loads after {} s", track.name,
                                       kSamplerLoadTimeout.count()));
        }
        plugin.idle();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::shared_ptr<engine::Plugin> renderPlugin(const model::Track& track, const model::PluginSlot& slot) {
    auto plugin = engine::createPlugin(slot.format, slot.bundle, slot.pluginId);
    if (!plugin) {
        throw AppError("cannot render track " + track.name + ": " + plugin.error());
    }
    if (!slot.state.empty() && !(*plugin)->loadState(slot.state)) {
        throw AppError("cannot render track " + track.name + ": " + slot.name + " did not load its settings");
    }
    if (slot.pluginId == kSamplerPluginId) {
        waitForSampler(track, **plugin);
    }
    return *std::move(plugin);
}

// Each render plays fresh instances with the saved states, so the live plugins stay with the live engine.
RenderInstruments attachInstruments(engine::Session& session, const model::Document& document) {
    RenderInstruments created;
    for (engine::Track& track : session.tracks) {
        const model::Track* source = model::findTrack(document, model::TrackId{track.id});
        if (source == nullptr) {
            continue;
        }
        if (source->instrument) {
            track.instrument = renderPlugin(*source, *source->instrument);
            created.push_back(track.instrument);
        }
        std::map<std::string, std::shared_ptr<engine::Plugin>> inserts;
        for (const model::Insert& insert : source->inserts) {
            if (insert.plugin) {
                inserts[insert.id.value] = renderPlugin(*source, *insert.plugin);
                created.push_back(inserts[insert.id.value]);
            }
        }
        attachPluginInserts(track, *source, [&inserts](const model::InsertId& id) {
            const auto found = inserts.find(id.value);
            return found == inserts.end() ? nullptr : found->second;
        });
    }
    for (engine::Track& track : session.tracks) {
        if (!hasInstrument(document, track.midiInTrack)) {
            track.midiInTrack.clear();
        }
    }
    return created;
}

// Track types put their devices in their state at bar 1, such as a Style's first section, as they do on play.
std::vector<OfflineRender::InstrumentMessage> chaseStart(const engine::Session& session, const Registry& registry,
                                                         const PlaybackContext& context) {
    std::vector<OfflineRender::InstrumentMessage> messages;
    for (const engine::Track& track : session.tracks) {
        const model::Track* source = model::findTrack(*context.document, model::TrackId{track.id});
        const TrackType* type = source != nullptr ? registry.trackType(source->kind) : nullptr;
        if (!track.instrument || type == nullptr || !type->chase) {
            continue;
        }
        for (MidiMessage& message : type->chase(*source, 0, context)) {
            if (message.port.empty()) {
                messages.push_back({.trackId = track.id, .bytes = std::move(message.bytes)});
            }
        }
    }
    return messages;
}

void waitForReadAhead(const engine::Engine& engine) {
    const auto deadline = std::chrono::steady_clock::now() + kReadAheadTimeout;
    while (!engine.readAheadReady()) {
        if (std::chrono::steady_clock::now() > deadline) {
            throw AppError("cannot render: audio files did not load within 5 s");
        }
        std::this_thread::yield();
    }
}

}  // namespace

std::size_t readAvailableCores() {
    std::size_t cores = std::max(1U, std::thread::hardware_concurrency());
    std::ifstream quota("/sys/fs/cgroup/cpu.max");
    std::string limit;
    double period = 0;
    double allowed = 0;
    if (quota >> limit >> period && period > 0 &&
        std::from_chars(limit.data(), limit.data() + limit.size(), allowed).ec == std::errc{}) {
        cores = std::min(cores, std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(allowed / period))));
    }
    return cores;
}

// Drops the frames a render's output comes late and hands the sink the rest in chunks of kChunk frames.
class OutputChunks {
public:
    OutputChunks(engine::Frames late, const OfflineRender::ChunkSink& sink)
        : late_(static_cast<std::size_t>(late)), sink_(&sink) {}

    void add(std::vector<std::vector<float>> chunk) {
        if (chunk.empty() || chunk.front().empty()) {
            return;
        }
        const auto offset = [](std::size_t frames) { return static_cast<std::ptrdiff_t>(frames); };
        const std::size_t dropped = std::min(late_, chunk.front().size());
        late_ -= dropped;
        for (std::vector<float>& channel : chunk) {
            channel.erase(channel.begin(), channel.begin() + offset(dropped));
        }
        const std::size_t size = chunk.front().size();
        const auto full = static_cast<std::size_t>(kChunk);
        if (carried_ == 0 && size == full) {
            (*sink_)(std::move(chunk));
            return;
        }
        carry_.resize(chunk.size());
        std::size_t used = 0;
        while (carried_ + size - used >= full) {
            const std::size_t take = full - carried_;
            for (std::size_t channel = 0; channel < chunk.size(); ++channel) {
                carry_[channel].insert(carry_[channel].end(), chunk[channel].begin() + offset(used),
                                       chunk[channel].begin() + offset(used + take));
            }
            (*sink_)(std::exchange(carry_, std::vector<std::vector<float>>(chunk.size())));
            used += take;
            carried_ = 0;
        }
        for (std::size_t channel = 0; channel < chunk.size(); ++channel) {
            carry_[channel].insert(carry_[channel].end(), chunk[channel].begin() + offset(used), chunk[channel].end());
        }
        carried_ += size - used;
    }

    void flush() {
        if (carried_ > 0) {
            (*sink_)(std::move(carry_));
            carried_ = 0;
        }
    }

private:
    std::size_t late_;
    const OfflineRender::ChunkSink* sink_;
    std::vector<std::vector<float>> carry_;
    std::size_t carried_ = 0;
};

engine::Frames renderFrames(const engine::Session& session, const model::Document& document, const Registry& registry,
                            double sampleRate) {
    const core::Ticks end = exportEnd(session, document);
    if (end <= 0) {
        throw AppError("the project has no clips to export");
    }
    registry.checkRenderSeconds(document.tempo.ticksToSeconds(end));
    return document.tempo.ticksToFrames(end, sampleRate);
}

SpilledAudio::SpilledAudio(std::size_t channels) : channels_(channels), file_(std::tmpfile()) {
    if (!file_) {
        throw AppError("cannot render: no temporary file to hold the mix");
    }
}

void SpilledAudio::append(std::span<const std::vector<float>> chunk) {
    for (const std::vector<float>& channel : chunk) {
        if (std::fwrite(channel.data(), sizeof(float), channel.size(), file_.get()) != channel.size()) {
            throw AppError("cannot render: the disk has no room for the temporary mix");
        }
    }
    chunkFrames_.push_back(chunk.front().size());
}

void SpilledAudio::replay(const engine::ChunkSink& sink) const {
    if (std::fseek(file_.get(), 0, SEEK_SET) != 0) {
        throw AppError("cannot render: the temporary mix did not read back");
    }
    std::vector<std::vector<float>> chunk(channels_);
    for (const std::size_t frames : chunkFrames_) {
        for (std::vector<float>& channel : chunk) {
            channel.resize(frames);
            if (std::fread(channel.data(), sizeof(float), frames, file_.get()) != frames) {
                throw AppError("cannot render: the temporary mix did not read back");
            }
        }
        sink(chunk);
    }
}

void SpilledAudio::CloseFile::operator()(std::FILE* file) const { std::fclose(file); }

model::Document liveDocument(ProjectController& controller) {
    return controller.instruments().withStates(controller.project().document());
}

PlaybackContext playbackContext(ProjectController& controller, const model::Document& document) {
    return {.tempo = document.tempo, .folder = controller.folder(), .document = &document};
}

core::Ticks exportEnd(const engine::Session& session, const model::Document& document) {
    core::Ticks last = -1;
    for (const engine::Track& track : session.tracks) {
        for (const engine::MidiClip& clip : track.midiClips) {
            if (!clip.events.empty()) {
                last = std::max(last, clip.start + clip.events.back().tick);
            }
            if (clip.length > 0) {
                last = std::max(last, clip.start + clip.length - 1);
            }
        }
    }
    const core::Ticks clipsEnd = projectEnd(document);
    const core::Ticks bar = document.tempo.ticksPerBar();
    return last < 0 || (clipsEnd > 0 && last <= clipsEnd) ? clipsEnd : ((last / bar) + 1) * bar;
}

std::vector<std::filesystem::path> exportStems(const engine::Session& session, const Registry& registry,
                                               const PlaybackContext& context, const std::filesystem::path& folder,
                                               double sampleRate, bool numbered, AudioEncoding encoding) {
    const model::Document& document = *context.document;
    const engine::Frames frames = renderFrames(session, document, registry, sampleRate);
    const std::vector<engine::Track> tracks = stemTracks(session, document);
    if (tracks.empty()) {
        throw AppError("there are no audio tracks to export");
    }
    std::filesystem::create_directories(folder);
    std::vector<std::filesystem::path> files;
    std::set<std::string> used;
    for (const engine::Track& track : tracks) {
        const auto source = std::ranges::find(document.tracks, model::TrackId{track.id}, &model::Track::id);
        std::string stem = fileStem(source != document.tracks.end() ? source->name : track.id);
        if (numbered) {
            const std::string number = std::format("{:02} ", source - document.tracks.begin() + 1);
            if (!stem.starts_with(number)) {
                stem.insert(0, number);
            }
        }
        if (!used.insert(stem).second) {
            stem += "-" + track.id;
        }
        files.push_back(folder / std::format("{}.{}", stem, audioFormatName(encoding.format)));
    }
    std::vector<AudioWriter> writers = files | std::views::transform([&](const std::filesystem::path& file) {
                                           return AudioWriter(file, 2, sampleRate, encoding);
                                       }) |
                                       std::ranges::to<std::vector>();
    OfflineRender render(
        captureSession(session, tracks | std::views::transform(&engine::Track::id) | std::ranges::to<std::vector>()),
        registry, context, sampleRate);
    render.process(
        frames,
        [&writers](const std::vector<std::vector<float>>& chunk) {
            for (std::size_t index = 0; index < writers.size(); ++index) {
                writers[index].write(std::span{chunk}.subspan(2 * index, 2));
            }
        },
        [&render] { render.idleInstruments(); });
    return files;
}

std::optional<MixLevels> exportMix(const engine::Session& session, const Registry& registry,
                                   const PlaybackContext& context, const std::filesystem::path& file, double sampleRate,
                                   const std::optional<LoudnessTarget>& target, AudioEncoding encoding) {
    const engine::Frames frames = renderFrames(session, *context.document, registry, sampleRate);
    if (!file.parent_path().empty()) {
        std::filesystem::create_directories(file.parent_path());
    }
    AudioWriter writer(file, 2, sampleRate, encoding);
    OfflineRender render(mixSession(session, *context.document), registry, context, sampleRate);
    const auto renderMix = [&](const engine::ChunkSink& sink) {
        render.process(
            frames, [&sink](const std::vector<std::vector<float>>& chunk) { sink(std::span{chunk}.first(2)); },
            [&render] { render.idleInstruments(); });
    };
    if (!target) {
        renderMix([&writer](std::span<const std::vector<float>> chunk) { writer.write(chunk); });
        return std::nullopt;
    }
    SpilledAudio mix(2);
    renderMix([&mix](std::span<const std::vector<float>> chunk) { mix.append(chunk); });
    return writeMix(mix, writer, sampleRate, target);
}

std::optional<MixLevels> writeMix(const SpilledAudio& mix, AudioWriter& writer, double sampleRate,
                                  const std::optional<LoudnessTarget>& target) {
    const engine::SignalReplay replay = [&mix](const engine::ChunkSink& sink) { mix.replay(sink); };
    if (!target) {
        replay([&writer](std::span<const std::vector<float>> chunk) { writer.write(chunk); });
        return std::nullopt;
    }
    const engine::LoudnessMatch match = engine::matchLoudness(replay, 2, sampleRate, target->lufs, target->ceilingDbtp);
    engine::TrackMeter meter(2, sampleRate);
    // We meter each chunk on another thread while the writer encodes it.
    std::future<void> metering;
    const auto write = [&](std::span<const std::vector<float>> chunk) {
        auto copy = std::make_shared<const std::vector<std::vector<float>>>(chunk.begin(), chunk.end());
        if (metering.valid()) {
            metering.get();
        }
        metering = std::async(std::launch::async, [&meter, copy] { meter.add(*copy); });
        writer.write(chunk);
    };
    if (match.gainDb) {
        engine::limitSignal(replay, 2, sampleRate, *match.gainDb, target->ceilingDbtp, write);
    } else {
        replay(write);
    }
    if (metering.valid()) {
        metering.get();
    }
    return MixLevels{.integratedLufs = match.integratedLufs, .truePeakDbtp = meter.levels().truePeakDbtp};
}

OfflineRender::OfflineRender(engine::Session session, const Registry& registry, const PlaybackContext& context,
                             double sampleRate, bool pipelineBuses, int inputChannels) {
    instruments_ = attachInstruments(session, *context.document);
    chase_ = chaseStart(session, registry, context);
    start(std::move(session), sampleRate, pipelineBuses, inputChannels);
}

OfflineRender::OfflineRender(engine::Session session, std::vector<std::shared_ptr<engine::Plugin>> instruments,
                             double sampleRate)
    : instruments_(std::move(instruments)) {
    start(std::move(session), sampleRate, true, 0);
}

void OfflineRender::start(engine::Session session, double sampleRate, bool pipelineBuses, int inputChannels) {
    for (engine::Track& track : session.tracks) {
        const bool fed = inputChannels > 0 && track.audioIn.deviceId == kRenderDevice;
        track.armed = false;
        track.monitor = fed;
        if (!fed) {
            track.audioIn = {};
        }
        track.midiIn.portId.clear();
        track.midiOut = {};
        track.midiThru = false;
    }
    session.clockPorts.clear();
    int outputs = 2;
    for (const engine::Track& track : session.tracks) {
        outputs =
            std::ranges::fold_left(track.captureOut.channels, outputs, [](int a, int b) { return std::max(a, b); });
        outputs = std::ranges::fold_left(track.chainOut.channels, outputs, [](int a, int b) { return std::max(a, b); });
    }
    auto backend = std::make_unique<engine::OfflineBackend>(
        std::vector<engine::AudioDeviceInfo>{{kRenderDevice, "Render", inputChannels, outputs}});
    offline_ = backend.get();
    engine_ = std::make_unique<engine::Engine>(
        std::move(backend),
        std::make_unique<engine::VirtualMidiBackend>(std::vector<std::string>{}, std::vector<std::string>{}),
        engine::Engine::Options{
            .sampleRate = sampleRate, .blockFrames = kBlock, .recordingDelay = 0, .pipelineBuses = pipelineBuses});
    try {
        engine_->setSession(std::move(session));
    } catch (const std::invalid_argument& error) {
        throw AppError(std::string{"cannot render: "} + error.what());
    } catch (const std::runtime_error& error) {
        throw AppError(std::string{"cannot render: "} + error.what());
    }
    if (!engine_->sessionErrors().empty()) {
        std::string message = "cannot render every clip:";
        for (const engine::SessionError& skipped : engine_->sessionErrors()) {
            message += " track " + skipped.trackId + ": " + skipped.message + ";";
        }
        throw AppError(message);
    }
    // We open the stream here, so the instruments activate on this thread and process never has to.
    engine_->keepWarm(true);
    engine_->locate(0);
    for (const auto& instrument : instruments_) {
        instrument->setOfflineRendering(true);
    }
}

OfflineRender::~OfflineRender() {
    for (const auto& instrument : instruments_) {
        instrument->setOfflineRendering(false);
    }
}

void OfflineRender::process(engine::Frames frames, const ChunkSink& sink, const std::function<void()>& idle,
                            const std::stop_token& stop, const ChunkFeed& feed, const ChunkSink& settled) {
    OutputChunks chunks(engine_->outputDelay(), sink);
    play(
        frames, [&chunks](std::vector<std::vector<float>> chunk) { chunks.add(std::move(chunk)); }, idle, stop, feed,
        settled);
    if (stop.stop_requested()) {
        return;
    }
    for (engine::Frames late = engine_->outputDelay(); late > 0; late -= kBlock) {
        offline_->process(std::min(kBlock, late));
        idle();
    }
    chunks.add(offline_->takeOutputs());
    chunks.flush();
}

void OfflineRender::play(engine::Frames frames, const ChunkSink& sink, const std::function<void()>& idle,
                         const std::stop_token& stop, const ChunkFeed& feed, const ChunkSink& settled) {
    const auto setInputs = [this](std::vector<std::vector<float>> inputs) {
        for (std::size_t channel = 0; channel < inputs.size(); ++channel) {
            offline_->setInput(kRenderDevice, static_cast<int>(channel) + 1, std::move(inputs[channel]));
        }
    };
    if (feed) {
        setInputs(feed(kSettleBlocks * kBlock));
    }
    // CLAP offers no way to ask a plugin whether it is ready, so we let the instruments settle for a few blocks.
    for (int block = 0; block < kSettleBlocks; ++block) {
        offline_->process(kBlock);
        idle();
    }
    waitForReadAhead(*engine_);
    for (const InstrumentMessage& message : chase_) {
        engine_->sendInstrumentMidi(message.trackId, message.bytes);
    }
    engine_->play();
    std::vector<std::vector<float>> settling = offline_->takeOutputs();
    if (settled) {
        settled(std::move(settling));
    }
    for (engine::Frames done = 0; done < frames; done += kChunk) {
        const engine::Frames chunk = std::min(kChunk, frames - done);
        if (feed) {
            setInputs(feed(chunk));
        }
        for (engine::Frames block = 0; block < chunk; block += kBlock) {
            if (stop.stop_requested()) {
                return;
            }
            offline_->process(std::min(kBlock, chunk - block));
            idle();
        }
        waitForReadAhead(*engine_);
        sink(offline_->takeOutputs());
    }
}

void appendStereo(std::vector<std::vector<float>>& stereo, const std::vector<std::vector<float>>& chunk) {
    for (const std::size_t channel : {0U, 1U}) {
        stereo[channel].insert(stereo[channel].end(), chunk[channel].begin(), chunk[channel].end());
    }
}

void OfflineRender::idleInstruments() {
    for (const auto& instrument : instruments_) {
        instrument->idle();
    }
}

bool OfflineRender::needsIdle() const {
    return std::ranges::any_of(instruments_, [](const auto& instrument) { return instrument->needsIdle(); });
}

std::vector<engine::RecordedTake> OfflineRender::recordMidi(const std::vector<std::string>& trackIds,
                                                            engine::Frames frames) {
    for (const std::string& id : trackIds) {
        engine_->setArmed(id, true);
    }
    engine_->setRecording(true);
    play(frames, [](const std::vector<std::vector<float>>&) {}, [this] { idleInstruments(); }, {}, {}, {});
    engine_->stop();
    offline_->process(kBlock);
    engine_->finishEndedTakes();
    return engine_->takeRecordings();
}

std::vector<engine::RecordedTake> recordMidiOffline(const engine::Session& session, const Registry& registry,
                                                    const PlaybackContext& context,
                                                    const std::vector<std::string>& trackIds, double sampleRate) {
    const engine::Frames frames = renderFrames(session, *context.document, registry, sampleRate);
    std::vector<engine::Track> tracks =
        session.tracks | std::views::transform(audible) | std::ranges::to<std::vector>();
    for (engine::Track& track : tracks) {
        track.audioOut = {};
    }
    OfflineRender render(renderSession(session, std::move(tracks)), registry, context, sampleRate);
    return render.recordMidi(trackIds, frames);
}

std::unique_ptr<OfflineRender> instrumentRender(std::shared_ptr<engine::Plugin> instrument, engine::MidiClip clip,
                                                const core::TempoMap& tempo, double sampleRate) {
    engine::Track track;
    track.id = "render";
    track.kind = engine::TrackKind::midi;
    track.audioOut = renderRoute();
    track.midiClips.push_back(std::move(clip));
    track.instrument = instrument;
    engine::Session session;
    session.tempo = tempo;
    session.tracks.push_back(std::move(track));
    return std::make_unique<OfflineRender>(
        std::move(session), std::vector<std::shared_ptr<engine::Plugin>>{std::move(instrument)}, sampleRate);
}

std::unique_ptr<OfflineRender> trackRender(const engine::Session& session, const Registry& registry,
                                           const PlaybackContext& context, const std::string& trackId,
                                           double sampleRate) {
    const auto track = std::ranges::find(session.tracks, trackId, &engine::Track::id);
    if (track == session.tracks.end()) {
        throw AppError("no track " + trackId + " in the session");
    }
    return std::make_unique<OfflineRender>(stemSession(session, *track), registry, context, sampleRate);
}

std::unique_ptr<OfflineRender> tracksRender(const engine::Session& session, const Registry& registry,
                                            const PlaybackContext& context, const std::vector<std::string>& trackIds,
                                            double sampleRate) {
    return std::make_unique<OfflineRender>(captureSession(session, trackIds), registry, context, sampleRate);
}

std::unique_ptr<OfflineRender> mixAndTracksRender(const engine::Session& session, const Registry& registry,
                                                  const PlaybackContext& context,
                                                  const std::vector<std::string>& trackIds, double sampleRate) {
    return std::make_unique<OfflineRender>(captureSession(session, trackIds, context.document), registry, context,
                                           sampleRate);
}

namespace {

// A process the service stops never runs its destructors, so each new SourceStems removes the stems of processes that
// ended.
void removeLeftStems() {
#ifndef _WIN32
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path(), error)) {
        const std::string name = entry.path().filename().string();
        if (!name.starts_with(kStemsPrefix)) {
            continue;
        }
        long pid = 0;
        const char* digits = name.data() + std::string_view{kStemsPrefix}.size();
        if (std::from_chars(digits, name.data() + name.size(), pid).ec == std::errc{} && pid > 0 &&
            ::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) {
            std::filesystem::remove_all(entry.path(), error);
        }
    }
#endif
}

}  // namespace

std::size_t availableCores() {
    static const std::size_t kCores = readAvailableCores();
    return kCores;
}

SourceStems::SourceStems()
    : folder_(std::filesystem::temp_directory_path() / std::format("{}{}-{:x}", kStemsPrefix,
                                                                   QCoreApplication::applicationPid(),
                                                                   reinterpret_cast<std::uintptr_t>(this))) {
    removeLeftStems();
    std::filesystem::create_directories(folder_);
}

SourceStems::~SourceStems() {
    std::error_code error;
    std::filesystem::remove_all(folder_, error);
}

std::filesystem::path SourceStems::file(const std::string& trackId) const { return folder_ / (trackId + ".wav"); }

namespace {

// Pushes each kept stem in the chunks its render pushed it in: the settling blocks, then the chunks of the render.
void replayStems(const SourceStems& stems, const std::vector<engine::Track>& tracks,
                 const std::vector<std::size_t>& sources, engine::Frames frames,
                 std::vector<std::unique_ptr<StemStream>>& streams) {
    struct CloseSound {
        void operator()(SNDFILE* file) const { sf_close(file); }
    };
    std::vector<std::unique_ptr<SNDFILE, CloseSound>> files;
    for (const std::size_t index : sources) {
        SF_INFO info{};
        files.emplace_back(engine::openSoundFile(stems.file(tracks[index].id), SFM_READ, &info));
        if (!files.back() || info.channels != 2) {
            throw AppError("cannot render: a kept stem did not read back");
        }
    }
    std::vector<engine::Frames> sizes{kSettleBlocks * kBlock};
    for (engine::Frames done = 0; done < frames; done += kChunk) {
        sizes.push_back(std::min(kChunk, frames - done));
    }
    std::vector<float> interleaved;
    for (const engine::Frames size : sizes) {
        for (std::size_t position = 0; position < sources.size(); ++position) {
            interleaved.resize(2 * static_cast<std::size_t>(size));
            if (sf_readf_float(files[position].get(), interleaved.data(), size) != size) {
                throw AppError("cannot render: a kept stem ended early");
            }
            std::vector<std::vector<float>> stem(2, std::vector<float>(static_cast<std::size_t>(size)));
            for (std::size_t frame = 0; frame < stem.front().size(); ++frame) {
                stem[0][frame] = interleaved[2 * frame];
                stem[1][frame] = interleaved[(2 * frame) + 1];
            }
            streams[sources[position]]->push(std::move(stem));
        }
    }
}

}  // namespace

void renderMixAndTracks(const engine::Session& session, const Registry& registry, const PlaybackContext& context,
                        const std::vector<std::string>& trackIds, double sampleRate, engine::Frames frames,
                        const OfflineRender::ChunkSink& sink, SourceStems* stems, const std::string& sourcesKey) {
    const model::Document& document = *context.document;
    const engine::Session capture = captureSession(session, trackIds, &document);
    const std::vector<engine::Track>& tracks = capture.tracks;
    const std::optional<MixGraph> graph = mixGraph(tracks);
    if (!graph) {
        OfflineRender render(capture, registry, context, sampleRate);
        render.process(frames, sink, [&render] { render.idleInstruments(); });
        return;
    }
    const std::size_t count = tracks.size();
    const std::vector<bool> sounds =
        std::views::iota(std::size_t{0}, count) | std::views::transform([&](std::size_t index) {
            return tracks[index].kind == engine::TrackKind::bus || playsAudio(tracks[index], document);
        }) |
        std::ranges::to<std::vector>();
    const auto byMixOrder = [&](std::vector<std::size_t> indices) {
        std::ranges::sort(indices, {}, [&](std::size_t index) { return graph->position[index]; });
        return indices;
    };

    // Tracks that take MIDI from each other play in one render, and every other track nothing feeds plays alone.
    std::vector<std::size_t> group = std::views::iota(std::size_t{0}, count) | std::ranges::to<std::vector>();
    const auto root = [&](std::size_t index) {
        while (group[index] != index) {
            index = group[index] = group[group[index]];
        }
        return index;
    };
    for (std::size_t index = 0; index < count; ++index) {
        const auto source = std::ranges::find(tracks, tracks[index].midiInTrack, &engine::Track::id);
        if (graph->level[index] == 0 && !tracks[index].midiInTrack.empty() && source != tracks.end() &&
            graph->level[static_cast<std::size_t>(source - tracks.begin())] == 0) {
            group[root(index)] = root(static_cast<std::size_t>(source - tracks.begin()));
        }
    }

    struct Stage {
        std::unique_ptr<OfflineRender> render;
        std::vector<std::size_t> reads;
        std::vector<std::size_t> writes;
        engine::Frames shift = 0;
    };
    std::deque<Stage> stages;
    const auto addStage = [&](std::vector<engine::Track> members, std::vector<std::size_t> reads,
                              std::vector<std::size_t> writes) -> Stage& {
        const std::vector<std::string> frozenIds =
            reads | std::views::transform([&](std::size_t index) { return tracks[index].id; }) |
            std::ranges::to<std::vector>();
        Stage& stage = stages.emplace_back(
            Stage{.render = nullptr, .reads = std::move(reads), .writes = std::move(writes), .shift = 0});
        const model::Document frozenDocumentOf = frozenDocument(document, frozenIds);
        stage.render = std::make_unique<OfflineRender>(
            renderSession(capture, std::move(members)), registry,
            PlaybackContext{.tempo = context.tempo, .folder = context.folder, .document = &frozenDocumentOf},
            sampleRate, false, static_cast<int>(2 * stage.reads.size()));
        return stage;
    };
    const auto frozenInputs = [&](const std::vector<std::size_t>& reads) {
        return std::views::iota(std::size_t{0}, reads.size()) | std::views::transform([&](std::size_t position) {
                   return frozen(tracks[reads[position]], static_cast<int>(2 * position) + 1);
               }) |
               std::ranges::to<std::vector>();
    };

    // At most one render per core: groups that share nothing take turns in the same render.
    const std::size_t cores = availableCores();
    const auto bucketsOf = [cores](const std::vector<std::size_t>& items) {
        std::vector<std::vector<std::size_t>> buckets(std::min(cores, items.size()));
        for (std::size_t position = 0; position < items.size(); ++position) {
            buckets[position % buckets.size()].push_back(items[position]);
        }
        return buckets;
    };
    const std::vector<std::size_t> groups =
        std::views::iota(std::size_t{0}, count) | std::views::filter([&](std::size_t index) {
            return graph->level[index] == 0 && root(index) == index &&
                   std::ranges::any_of(std::views::iota(std::size_t{0}, count), [&](std::size_t member) {
                       return sounds[member] && graph->level[member] == 0 && root(member) == index;
                   });
        }) |
        std::ranges::to<std::vector>();
    const std::vector<std::size_t> sources =
        std::views::iota(std::size_t{0}, count) |
        std::views::filter([&](std::size_t index) { return sounds[index] && graph->level[index] == 0; }) |
        std::ranges::to<std::vector>();
    std::string key;
    if (stems != nullptr && !sourcesKey.empty()) {
        key = std::format("{}\n{} frames at {} Hz", sourcesKey, frames, sampleRate);
        for (const std::size_t index : sources) {
            key += "\n" + tracks[index].id;
        }
    }
    const bool replay = stems != nullptr && stems->holds(key);
    if (stems != nullptr) {
        stems->forget();
    }
    for (const std::vector<std::size_t>& bucket :
         replay ? std::vector<std::vector<std::size_t>>{} : bucketsOf(groups)) {
        std::vector<engine::Track> members;
        std::vector<std::size_t> writes;
        for (std::size_t index = 0; index < count; ++index) {
            if (graph->level[index] != 0 || !std::ranges::contains(bucket, root(index))) {
                continue;
            }
            engine::Track track = tracks[index];
            track.audioOut = {};
            track.outputBus.clear();
            track.sends.clear();
            track.captureOut = {};
            if (sounds[index]) {
                const auto channel = static_cast<int>(2 * writes.size()) + 1;
                track.chainOut = {kRenderDevice, {channel, channel + 1}};
                writes.push_back(index);
            }
            members.push_back(std::move(track));
        }
        addStage(std::move(members), {}, std::move(writes));
    }
    // Each render delays its tracks to its own slowest one, so we delay its stems by what it lacks to the slowest of
    // all.
    const engine::Frames slowest = std::ranges::fold_left(
        stages | std::views::transform([](const Stage& stage) { return stage.render->latencyCompensation(); }),
        engine::Frames{0}, [](engine::Frames a, engine::Frames b) { return std::max(a, b); });
    for (Stage& stage : stages) {
        stage.shift = slowest - stage.render->latencyCompensation();
    }
    const std::size_t levels = std::ranges::max(graph->level) + 1;
    for (std::size_t level = 1; level < levels; ++level) {
        const std::vector<std::size_t> buses =
            byMixOrder(std::views::iota(std::size_t{0}, count) |
                       std::views::filter([&](std::size_t index) { return graph->level[index] == level; }) |
                       std::ranges::to<std::vector>());
        for (const std::vector<std::size_t>& bucket : bucketsOf(buses)) {
            const auto inBucket = [&](const std::string& id) {
                return std::ranges::any_of(bucket, [&](std::size_t bus) { return tracks[bus].id == id; });
            };
            std::vector<std::size_t> feeding;
            for (std::size_t index = 0; index < count; ++index) {
                if (sounds[index] && std::ranges::any_of(graph->fed[index], [&](std::size_t bus) {
                        return std::ranges::contains(bucket, bus);
                    })) {
                    feeding.push_back(index);
                }
            }
            feeding = byMixOrder(std::move(feeding));
            std::vector<engine::Track> members = frozenInputs(feeding);
            for (engine::Track& track : members) {
                std::erase_if(track.sends, [&](const engine::Send& send) { return !inBucket(send.bus); });
                if (!inBucket(track.outputBus)) {
                    track.outputBus.clear();
                }
            }
            for (std::size_t position = 0; position < bucket.size(); ++position) {
                engine::Track target = tracks[bucket[position]];
                target.audioOut = {};
                target.outputBus.clear();
                target.sends.clear();
                target.captureOut = {};
                const auto channel = static_cast<int>(2 * position) + 1;
                target.chainOut = {kRenderDevice, {channel, channel + 1}};
                members.push_back(std::move(target));
            }
            addStage(std::move(members), std::move(feeding), bucket);
        }
    }
    const std::vector<std::size_t> mixed =
        byMixOrder(std::views::iota(std::size_t{0}, count) |
                   std::views::filter([&](std::size_t index) { return static_cast<bool>(sounds[index]); }) |
                   std::ranges::to<std::vector>());
    std::vector<engine::Track> mixTracks = frozenInputs(mixed);
    for (std::size_t position = 0; position < mixed.size(); ++position) {
        const engine::Track& source = tracks[mixed[position]];
        engine::Track& track = mixTracks[position];
        track.audioOut = source.outputBus.empty() ? source.audioOut : engine::AudioRoute{};
        track.captureOut = source.captureOut;
        track.outputBus.clear();
        track.sends.clear();
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!sounds[index]) {
            engine::Track silent = frozen(tracks[index], std::nullopt);
            silent.captureOut = tracks[index].captureOut;
            mixTracks.push_back(std::move(silent));
        }
    }
    const std::size_t mixStage = stages.size();
    addStage(std::move(mixTracks), mixed, {});

    std::vector<std::size_t> readers(count);
    for (const Stage& stage : stages) {
        for (const std::size_t index : stage.reads) {
            ++readers[index];
        }
    }
    std::vector<std::unique_ptr<StemStream>> streams(count);
    for (std::size_t index = 0; index < count; ++index) {
        streams[index] = std::make_unique<StemStream>(readers[index]);
    }
    MainThreadWork work;
    const std::vector<std::function<void()>> jobs =
        std::views::iota(std::size_t{0}, stages.size()) | std::views::transform([&](std::size_t number) {
            return std::function<void()>([&, number] {
                Stage& stage = stages[number];
                OfflineRender& render = *stage.render;
                FailedStage outcome(stage.writes, stage.reads, streams);
                std::size_t chunk = 0;
                std::vector<std::vector<float>> late(2 * stage.writes.size(),
                                                     std::vector<float>(static_cast<std::size_t>(stage.shift)));
                // Sources also keep their stems, so a later render whose sources play the same skips them.
                std::vector<AudioWriter> kept;
                if (!key.empty() && stage.reads.empty()) {
                    for (const std::size_t index : stage.writes) {
                        kept.emplace_back(stems->file(tracks[index].id), 2, sampleRate);
                    }
                }
                const auto pushStems = [&](std::vector<std::vector<float>> output) {
                    for (std::size_t position = 0; position < stage.writes.size(); ++position) {
                        std::vector<std::vector<float>> stem(2);
                        for (std::size_t side = 0; side < 2; ++side) {
                            std::vector<float>& carried = late[(2 * position) + side];
                            std::vector<float>& played = output[(2 * position) + side];
                            const std::size_t size = played.size();
                            carried.insert(carried.end(), played.begin(), played.end());
                            stem[side].assign(carried.begin(), carried.begin() + static_cast<std::ptrdiff_t>(size));
                            carried.erase(carried.begin(), carried.begin() + static_cast<std::ptrdiff_t>(size));
                        }
                        if (!kept.empty()) {
                            kept[position].write(stem);
                        }
                        streams[stage.writes[position]]->push(std::move(stem));
                    }
                };
                const OfflineRender::ChunkFeed feed = [&](engine::Frames) {
                    std::vector<std::vector<float>> inputs;
                    for (const std::size_t index : stage.reads) {
                        const auto stem = streams[index]->read(chunk);
                        inputs.insert(inputs.end(), stem->begin(), stem->end());
                    }
                    ++chunk;
                    return inputs;
                };
                const auto idle = [&render, &work] {
                    if (render.needsIdle()) {
                        work.idle(render);
                    }
                };
                // The instruments sound while they settle, so every render plays what its sources played then too.
                const OfflineRender::ChunkFeed fed = stage.reads.empty() ? OfflineRender::ChunkFeed{} : feed;
                if (number == mixStage) {
                    render.process(frames, sink, idle, {}, fed);
                } else {
                    render.process(frames, pushStems, idle, {}, fed, pushStems);
                }
                outcome.played();
            });
        }) |
        std::ranges::to<std::vector>();
    std::vector<std::function<void()>> all = jobs;
    if (replay) {
        all.emplace_back([&] {
            FailedStage outcome(sources, {}, streams);
            replayStems(*stems, tracks, sources, frames, streams);
            outcome.played();
        });
    }
    work.run(all);
    if (!key.empty()) {
        stems->keep(key);
    }
}

}  // namespace emerick::app
