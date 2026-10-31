#include "emerick/app/track_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/app/stem_export.hpp"
#include "emerick/engine/audio_analysis.hpp"

namespace emerick::app {

namespace {

constexpr const char* kActionId = "track.analyze";

// Everything the render reads, copied on the UI thread, so the worker thread never touches the live project.
struct TrackRender {
    model::Document document;
    engine::Session session;
    std::filesystem::path folder;
    // Empty for the mix.
    std::string trackId;
    core::Ticks from = 0;
    core::Ticks to = 0;
    double sampleRate = 0;
};

TrackRender prepareRender(ProjectController& controller, const Registry& registry, const Json& params) {
    TrackRender job;
    job.document = controller.instruments().withStates(controller.project().document());
    job.folder = controller.folder();
    job.sampleRate = controller.sampleRate();
    if (const auto track = optionalParam<std::string>(params, "track")) {
        const model::TrackId id = findTrackByIdOrName(controller.project(), *track);
        const model::Track& source = *model::findTrack(job.document, id);
        const TrackType* type = registry.trackType(source.kind);
        if (type == nullptr || !playsAudio(*type, source)) {
            throw AppError(playsNoAudio(type, source, "pick an audio track or a bus"));
        }
        job.trackId = id.value;
    }
    job.session = toSession(job.document, registry, job.folder);
    const core::TempoMap& tempo = job.document.tempo;
    const core::Ticks end = exportEnd(job.session, job.document);
    if (end <= 0) {
        throw AppError("the project has no clips to analyze, add a clip first");
    }
    job.from = optionalPosition(params, "from", tempo).value_or(0);
    job.to = std::min(optionalPosition(params, "to", tempo).value_or(end), end);
    if (job.from < 0 || job.from >= job.to) {
        throw AppError(std::format("give from before to, both from {} to the project end {}",
                                   tempo.toBarBeatTick(0).format(), tempo.toBarBeatTick(end).format()));
    }
    return job;
}

Json levelDb(double value) { return std::isfinite(value) ? Json(std::round(value * 10) / 10) : Json(nullptr); }

Json analyzeRender(const TrackRender& job, const Registry& registry) {
    const core::TempoMap& tempo = job.document.tempo;
    const PlaybackContext context{.tempo = tempo, .folder = job.folder, .document = &job.document};
    std::vector<std::vector<float>> channels = renderTrack(job.session, registry, context, job.trackId,
                                                           tempo.ticksToFrames(job.to, job.sampleRate), job.sampleRate);
    const auto skipped = static_cast<std::ptrdiff_t>(tempo.ticksToFrames(job.from, job.sampleRate));
    for (std::vector<float>& channel : channels) {
        channel.erase(channel.begin(), channel.begin() + std::min(skipped, std::ssize(channel)));
    }
    const engine::AudioAnalysis levels = engine::analyzeAudio(channels, {.sampleRate = job.sampleRate});
    const engine::ToneBalance tone = engine::analyzeTone(channels, job.sampleRate);
    Json bands = Json::object();
    for (const auto& [band, share] : std::views::zip(engine::kToneBands, tone.bandShare)) {
        bands[std::string{band.name}] = std::round(share * 1000) / 1000;
    }
    return {{"track", job.trackId.empty() ? Json(nullptr) : Json(job.trackId)},
            {"from", positionState(job.from, tempo)},
            {"to", positionState(job.to, tempo)},
            {"integratedLufs", levelDb(levels.integratedLufs)},
            {"samplePeakDbfs", levelDb(levels.samplePeakDbfs)},
            {"truePeakDbtp", levelDb(levels.truePeakDbtp)},
            {"activeRmsDb", levelDb(tone.activeRmsDb)},
            {"bandShare", bands},
            {"centroidHz", std::round(tone.centroidHz)},
            {"message", std::format("{:.1f} LUFS integrated, {:.1f} dBTP true peak, {:.1f} dB active RMS",
                                    levels.integratedLufs, levels.truePeakDbtp, tone.activeRmsDb)}};
}

}  // namespace

void registerTrackAnalysis(Registry& registry, ProjectController& controller) {
    registry.addAction(
        {.id = kActionId,
         .title = "Analyze track",
         .description =
             "Render a track or bus alone and dry as its stem does, or the whole mix when track is omitted, and "
             "measure integrated LUFS, sample peak, true peak, active RMS (the median of 100 ms blocks above -60 "
             "dBFS), each band's share of the energy (low below 120 Hz, lowMid 120 to 500 Hz, mid 500 Hz to 2 kHz, "
             "highMid 2 to 6 kHz, high above 6 kHz) and the spectral centroid in Hz. from and to take ticks or "
             "bar.beat.tick and default to the project start and end.",
         .keywords = {"loudness", "lufs", "true peak", "rms", "spectrum", "tone", "balance", "centroid"},
         .category = "Analyze",
         .schema = {{"properties",
                     {{"track", {{"type", "string"}}}, {"from", Json::object()}, {"to", Json::object()}}}},
         .run =
             [&controller, &registry](const Json& params) {
                 return analyzeRender(prepareRender(controller, registry, params), registry);
             },
         .notifies = true});
}

TrackAnalysis::TrackAnalysis(const Registry& registry, ProjectController& controller, QObject* parent)
    : QObject(parent), registry_(registry), controller_(controller) {}

void TrackAnalysis::analyze(const QString& track) {
    if (!busyTrack_.isEmpty()) {
        return;
    }
    const Json params{{"track", track.toStdString()}};
    std::optional<TrackRender> job;
    try {
        job = prepareRender(controller_, registry_, params);
    } catch (const AppError& error) {
        emit registry_.noticeRaised(qs(registry_.action(kActionId)->title), qs(error.what()));
        return;
    }
    emit registry_.actionRan(kActionId, params, ActionSource::Ui);
    busyTrack_ = track;
    emit busyTrackChanged();
    worker_ = std::jthread([this, track, job = std::move(*job)] {
        Json result;
        QString error;
        try {
            result = analyzeRender(job, registry_);
        } catch (const AppError& failure) {
            error = qs(failure.what());
        }
        QMetaObject::invokeMethod(
            this, [this, track, result = std::move(result), error] { finish(track, result, error); },
            Qt::QueuedConnection);
    });
}

void TrackAnalysis::finish(const QString& track, const Json& result, const QString& error) {
    busyTrack_.clear();
    emit busyTrackChanged();
    if (!error.isEmpty()) {
        emit registry_.noticeRaised(qs(registry_.action(kActionId)->title), error);
        return;
    }
    results_.insert(track, toVariant(result));
    emit resultsChanged();
}

}  // namespace emerick::app
