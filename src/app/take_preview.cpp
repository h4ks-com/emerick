#include <QString>
#include <algorithm>
#include <iterator>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/project_controller.hpp"

namespace emerick::app {

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
    const auto ended = previews_ | std::views::filter([&](const auto& entry) {
                           return !growing.contains(entry.first) && entry.second.audioFile.empty() &&
                                  entry.second.recorded.midiEvents.empty();
                       }) |
                       std::views::keys | std::ranges::to<std::vector>();
    for (const std::string& id : ended) {
        endPreview(id);
    }
}

void ProjectController::updatePreview(const std::string& trackId, engine::TakeProgress progress) {
    TakePreview& preview = previews_[trackId];
    if (preview.take != progress.take) {
        preview = {};
        preview.take = progress.take;
        preview.start = progress.startTick;
        preview.audioFile = progress.audioFile;
        preview.recorded.trackId = trackId;
        preview.recorded.startTick = progress.startTick;
    }
    const core::TempoMap& tempo = project_.document().tempo;
    const double rate = engine_.layout().sampleRate;
    preview.frames = progress.frames;
    std::ranges::move(progress.midiEvents, std::back_inserter(preview.recorded.midiEvents));
    if (!preview.audioFile.empty()) {
        emit takePeaks(preview.audioFile, Peaks{std::move(progress.peakMinimum), std::move(progress.peakMaximum)});
        preview.length =
            tempo.framesToTicks(tempo.ticksToFrames(preview.start, rate) + preview.frames, rate) - preview.start;
        emit takePreviewChanged(QString::fromStdString(trackId));
        return;
    }
    preview.length = std::max<core::Ticks>(engine_.position().ticks - preview.start, 0);
    const model::Track* track = project_.track(model::TrackId{trackId});
    const TrackType* type = track == nullptr ? nullptr : registry_.trackType(track->kind);
    preview.track.reset();
    if (type != nullptr && type->fromTake && !preview.recorded.midiEvents.empty()) {
        model::Track shown = *track;
        shown.clips.clear();
        const TakeContext context{
            .tempo = tempo, .folder = folder_, .stopTicks = preview.start + preview.length, .sampleRate = rate};
        // A plugin that cannot read its data yet keeps showing the track as it is.
        try {
            type->fromTake(shown, preview.recorded, context);
            preview.track = std::move(shown);
        } catch (const AppError&) {
            preview.track.reset();
        }
    }
    emit takePreviewChanged(QString::fromStdString(trackId));
}

void ProjectController::endPreview(const std::string& trackId) {
    if (previews_.erase(trackId) > 0) {
        emit takePreviewChanged(QString::fromStdString(trackId));
    }
}

}  // namespace emerick::app
