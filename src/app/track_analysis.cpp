#include "emerick/app/track_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <format>
#include <memory>
#include <mutex>
#include <ranges>
#include <stop_token>
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

// What the analysis measures of a render, which the worker thread reads.
struct AnalysisRange {
    core::TempoMap tempo;
    // Empty for the mix.
    std::string trackId;
    core::Ticks from = 0;
    core::Ticks to = 0;
    double sampleRate = 0;

    [[nodiscard]] engine::Frames endFrame() const { return tempo.ticksToFrames(to, sampleRate); }
};

struct TrackRender {
    std::unique_ptr<OfflineRender> render;
    AnalysisRange range;
};

TrackRender prepareRender(ProjectController& controller, const Registry& registry, const Json& params) {
    const model::Document document = controller.instruments().withStates(controller.project().document());
    AnalysisRange range;
    range.tempo = document.tempo;
    range.sampleRate = controller.sampleRate();
    if (const auto track = optionalParam<std::string>(params, "track")) {
        const model::TrackId id = findTrackByIdOrName(controller.project(), *track);
        const model::Track& source = *model::findTrack(document, id);
        const TrackType* type = registry.trackType(source.kind);
        if (type == nullptr || !playsAudio(*type, source)) {
            throw AppError(playsNoAudio(type, source, "pick an audio track or a bus"));
        }
        range.trackId = id.value;
    }
    const engine::Session session = toSession(document, registry, controller.folder());
    const core::TempoMap& tempo = document.tempo;
    const core::Ticks end = exportEnd(session, document);
    if (end <= 0) {
        throw AppError("the project has no clips to analyze, add a clip first");
    }
    range.from = optionalPosition(params, "from", tempo).value_or(0);
    range.to = std::min(optionalPosition(params, "to", tempo).value_or(end), end);
    if (range.from < 0 || range.from >= range.to) {
        throw AppError(std::format("give from before to, both from {} to the project end {}",
                                   tempo.toBarBeatTick(0).format(), tempo.toBarBeatTick(end).format()));
    }
    const PlaybackContext context{.tempo = tempo, .folder = controller.folder(), .document = &document};
    return {.render = trackRender(session, registry, context, range.trackId, range.sampleRate),
            .range = std::move(range)};
}

Json levelDb(double value) { return std::isfinite(value) ? Json(std::round(value * 10) / 10) : Json(nullptr); }

Json measure(const AnalysisRange& range, std::vector<std::vector<float>> channels) {
    const core::TempoMap& tempo = range.tempo;
    const auto skipped = static_cast<std::ptrdiff_t>(tempo.ticksToFrames(range.from, range.sampleRate));
    for (std::vector<float>& channel : channels) {
        channel.erase(channel.begin(), channel.begin() + std::min(skipped, std::ssize(channel)));
    }
    const engine::AudioAnalysis levels = engine::analyzeAudio(channels, {.sampleRate = range.sampleRate});
    const engine::ToneBalance tone = engine::analyzeTone(channels, range.sampleRate);
    Json bands = Json::object();
    for (const auto& [band, share] : std::views::zip(engine::kToneBands, tone.bandShare)) {
        bands[std::string{band.name}] = std::round(share * 1000) / 1000;
    }
    return {{"track", range.trackId.empty() ? Json(nullptr) : Json(range.trackId)},
            {"from", positionState(range.from, tempo)},
            {"to", positionState(range.to, tempo)},
            {"integratedLufs", levelDb(levels.integratedLufs)},
            {"samplePeakDbfs", levelDb(levels.samplePeakDbfs)},
            {"truePeakDbtp", levelDb(levels.truePeakDbtp)},
            {"activeRmsDb", levelDb(tone.activeRmsDb)},
            {"bandShare", bands},
            {"centroidHz", std::round(tone.centroidHz)},
            {"message", std::format("{:.1f} LUFS integrated, {:.1f} dBTP true peak, {:.1f} dB active RMS",
                                    levels.integratedLufs, levels.truePeakDbtp, tone.activeRmsDb)}};
}

Json analyzeRender(const TrackRender& job) {
    OfflineRender& render = *job.render;
    return measure(job.range, render.process(job.range.endFrame(), [&render] { render.idleInstruments(); }));
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
         .run = [&controller,
                 &registry](const Json& params) { return analyzeRender(prepareRender(controller, registry, params)); },
         .notifies = true});
}

TrackAnalysis::TrackAnalysis(const Registry& registry, ProjectController& controller, QObject* parent)
    : QObject(parent), registry_(registry), controller_(controller) {}

void TrackAnalysis::analyze(const QString& track) {
    if (!busyTrack_.isEmpty()) {
        return;
    }
    const Json params{{"track", track.toStdString()}};
    TrackRender job;
    try {
        job = prepareRender(controller_, registry_, params);
    } catch (const AppError& error) {
        emit registry_.noticeRaised(qs(registry_.action(kActionId)->title), qs(error.what()));
        return;
    }
    emit registry_.actionRan(kActionId, params, ActionSource::Ui);
    busyTrack_ = track;
    emit busyTrackChanged();
    render_ = std::move(job.render);
    worker_ = std::jthread([this, track, range = std::move(job.range)](const std::stop_token& stop) {
        Json result;
        QString error;
        try {
            std::vector<std::vector<float>> channels =
                render_->process(range.endFrame(), [this, &stop] { idleOnUiThread(stop); }, stop);
            if (stop.stop_requested()) {
                return;
            }
            result = measure(range, std::move(channels));
        } catch (const AppError& failure) {
            error = qs(failure.what());
        }
        QMetaObject::invokeMethod(
            this, [this, track, result = std::move(result), error] { finish(track, result, error); },
            Qt::QueuedConnection);
    });
}

void TrackAnalysis::idleOnUiThread(const std::stop_token& stop) {
    std::mutex mutex;
    std::condition_variable_any idled;
    bool done = false;
    QMetaObject::invokeMethod(
        this,
        [this, &mutex, &idled, &done] {
            render_->idleInstruments();
            const std::scoped_lock lock(mutex);
            done = true;
            idled.notify_one();
        },
        Qt::QueuedConnection);
    std::unique_lock lock(mutex);
    idled.wait(lock, stop, [&done] { return done; });
}

void TrackAnalysis::finish(const QString& track, const Json& result, const QString& error) {
    worker_.join();
    render_.reset();
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
