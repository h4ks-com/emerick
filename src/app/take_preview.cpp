#include <QString>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/project_controller.hpp"
#include "emerick/core/midi.hpp"

namespace emerick::app {

namespace {

// A take's preview freezes in chunks of this many bars, so an update only redoes the chunk the playhead is in.
constexpr core::Ticks kChunkBars = 4;

// We carry held notes into the next chunk as note ons at tick 0, so it keeps drawing them.
std::vector<engine::MidiEvent> heldNotes(const std::vector<engine::MidiEvent>& events) {
    std::vector<engine::MidiEvent> held;
    for (const engine::MidiEvent& event : events) {
        if (event.bytes.size() < 3) {
            continue;
        }
        const auto type = static_cast<std::uint8_t>(event.bytes[0] & 0xF0);
        if (type == core::midi::kNoteOn && event.bytes[2] > 0) {
            held.push_back({0, event.bytes});
        } else if (type == core::midi::kNoteOff || type == core::midi::kNoteOn) {
            const auto on = std::ranges::find_if(held, [&](const engine::MidiEvent& note) {
                return note.bytes[0] == (core::midi::kNoteOn | (event.bytes[0] & 0x0F)) &&
                       note.bytes[1] == event.bytes[1];
            });
            if (on != held.end()) {
                held.erase(on);
            }
        }
    }
    return held;
}

// A plugin that cannot read its data yet keeps showing the track as it is.
bool applyTake(const TrackType& type, model::Track& track, const engine::RecordedTake& take,
               const TakeContext& context) {
    try {
        type.fromTake(track, take, context);
        return true;
    } catch (const AppError&) {
        return false;
    }
}

model::Track liveTrack(model::Track& frozen) {
    std::vector<model::Clip> clips = std::exchange(frozen.clips, {});
    model::Track shown = frozen;
    frozen.clips = std::move(clips);
    return shown;
}

void freezeChunk(engine::RecordedTake& live, model::Track& frozen, const TrackType& type, core::Ticks chunk,
                 TakeContext context) {
    const auto later = std::ranges::stable_partition(
        live.midiEvents, [&](const engine::MidiEvent& event) { return event.tick < chunk; });
    engine::RecordedTake passed;
    passed.trackId = live.trackId;
    passed.startTick = live.startTick;
    passed.midiEvents.assign(std::make_move_iterator(live.midiEvents.begin()), std::make_move_iterator(later.begin()));
    live.midiEvents.erase(live.midiEvents.begin(), later.begin());
    context.stopTicks = live.startTick + chunk;
    applyTake(type, frozen, passed, context);
    for (engine::MidiEvent& event : live.midiEvents) {
        event.tick -= chunk;
    }
    std::ranges::stable_sort(passed.midiEvents, {}, &engine::MidiEvent::tick);
    const std::vector<engine::MidiEvent> held = heldNotes(passed.midiEvents);
    live.midiEvents.insert(live.midiEvents.begin(), held.begin(), held.end());
    live.startTick += chunk;
}

}  // namespace

const TakePreview* ProjectController::takePreview(const model::TrackId& id) const {
    const auto found = previews_.find(id.value);
    return found == previews_.end() ? nullptr : &found->second;
}

void ProjectController::updatePreviews() {
    std::set<std::string> growing;
    for (engine::TakeProgress& progress : engine_.takeProgress()) {
        growing.insert(progress.trackId);
        const std::string trackId = progress.trackId;
        updatePreview(trackId, std::move(progress));
    }
    // A take with audio or MIDI ends its preview when it lands; an empty MIDI take never lands.
    const auto ended =
        previews_ | std::views::filter([&](const auto& entry) {
            return !growing.contains(entry.first) && entry.second.audioFile.empty() && !entry.second.heardMidi;
        }) |
        std::views::keys | std::ranges::to<std::vector>();
    for (const std::string& id : ended) {
        endPreview(id);
    }
}

void ProjectController::updatePreview(const std::string& trackId, engine::TakeProgress progress) {
    TakePreview& preview = previews_[trackId];
    const model::Track* track = project_.track(model::TrackId{trackId});
    if (preview.take != progress.take) {
        preview = {};
        preview.take = progress.take;
        preview.start = progress.startTick;
        preview.audioFile = progress.audioFile;
        preview.live.trackId = trackId;
        preview.live.startTick = progress.startTick;
        if (track != nullptr) {
            preview.frozen = *track;
            preview.frozen->clips.clear();
        }
    }
    const core::TempoMap& tempo = project_.document().tempo;
    const double rate = engine_.layout().sampleRate;
    preview.frames = progress.frames;
    if (!preview.audioFile.empty()) {
        emit takePeaks(preview.audioFile, Peaks{std::move(progress.peakMinimum), std::move(progress.peakMaximum)});
        preview.length =
            tempo.framesToTicks(tempo.ticksToFrames(preview.start, rate) + preview.frames, rate) - preview.start;
        emit takePreviewChanged(QString::fromStdString(trackId), false);
        return;
    }
    preview.heardMidi = preview.heardMidi || !progress.midiEvents.empty();
    for (engine::MidiEvent& event : progress.midiEvents) {
        event.tick -= preview.live.startTick - preview.start;
        preview.live.midiEvents.push_back(std::move(event));
    }
    const core::Ticks now = engine_.position().ticks;
    preview.length = std::max<core::Ticks>(now - preview.start, 0);
    const TrackType* type = track == nullptr ? nullptr : registry_.trackType(track->kind);
    std::optional<model::Track> previous = std::exchange(preview.track, std::nullopt);
    if (type != nullptr && type->fromTake && preview.frozen && preview.heardMidi) {
        const TakeContext context{
            .tempo = tempo, .folder = folder_, .stopTicks = preview.start + preview.length, .sampleRate = rate};
        const core::Ticks chunk = kChunkBars * tempo.ticksPerBar();
        // Lane kinds such as chords decode a take with state that runs across it, so we only chunk clip kinds, whose
        // notes stand alone. We freeze a chunk a bar after the playhead leaves it, so late events still land in it.
        while (type->laneComponent.empty() && now >= preview.live.startTick + chunk + tempo.ticksPerBar()) {
            freezeChunk(preview.live, *preview.frozen, *type, chunk, context);
        }
        model::Track shown = liveTrack(*preview.frozen);
        if (applyTake(*type, shown, preview.live, context)) {
            preview.track = std::move(shown);
        }
    }
    const bool dataChanged =
        previous.has_value() != preview.track.has_value() || (previous && previous->data != preview.track->data);
    emit takePreviewChanged(QString::fromStdString(trackId), dataChanged);
}

void ProjectController::endPreview(const std::string& trackId) {
    if (previews_.erase(trackId) > 0) {
        emit takePreviewChanged(QString::fromStdString(trackId), true);
    }
}

}  // namespace emerick::app
