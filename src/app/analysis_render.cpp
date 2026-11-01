#include "emerick/app/analysis_render.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <functional>
#include <future>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/app/stem_export.hpp"
#include "emerick/app/track_analysis.hpp"
#include "emerick/engine/audio_analysis.hpp"
#include "emerick/engine/mix_issues.hpp"
#include "emerick/engine/window_meter.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

namespace {

constexpr double kMinWindowSeconds = 0.05;
constexpr std::size_t kMaxWindows = 20000;
constexpr const char* kMixId = "master";

using TickStep = core::Ticks (core::TempoMap::*)() const;
constexpr std::array<std::string_view, 2> kResolutionNames{"bar", "beat"};
constexpr std::array<TickStep, 2> kResolutionSteps{&core::TempoMap::ticksPerBar, &core::TempoMap::ticksPerBeat};

struct WindowStart {
    core::Ticks tick = 0;
    engine::Frames frame = 0;
};

struct AnalysisRange {
    core::TempoMap tempo;
    core::Ticks from = 0;
    core::Ticks to = 0;
    double sampleRate = 0;
    std::vector<WindowStart> windows;

    [[nodiscard]] engine::Frames frameOf(core::Ticks ticks) const { return tempo.ticksToFrames(ticks, sampleRate); }

    [[nodiscard]] engine::Frames windowEnd(std::size_t index) const {
        return index + 1 < windows.size() ? windows[index + 1].frame : frameOf(to);
    }

    [[nodiscard]] core::Ticks windowEndTick(std::size_t index) const {
        return index + 1 < windows.size() ? windows[index + 1].tick : to;
    }

    [[nodiscard]] double windowBeats(std::size_t index) const {
        const double seconds = static_cast<double>(windowEnd(index) - windows[index].frame) / sampleRate;
        return seconds / tempo.ticksToSeconds(tempo.ticksPerBeat());
    }
};

std::vector<WindowStart> tickWindows(const AnalysisRange& range, core::Ticks step) {
    std::vector<WindowStart> windows;
    for (core::Ticks tick = range.from; tick < range.to; tick = ((tick / step) + 1) * step) {
        windows.push_back({tick, range.frameOf(tick)});
    }
    return windows;
}

std::vector<WindowStart> secondWindows(const AnalysisRange& range, double seconds) {
    if (!(seconds >= kMinWindowSeconds)) {
        throw AppError(std::format("give resolution as bar, beat or at least {} seconds", kMinWindowSeconds));
    }
    const engine::Frames first = range.frameOf(range.from);
    const engine::Frames last = range.frameOf(range.to);
    std::vector<WindowStart> windows;
    for (std::size_t index = 0;; ++index) {
        const engine::Frames frame =
            first + static_cast<engine::Frames>(std::llround(static_cast<double>(index) * seconds * range.sampleRate));
        if (frame >= last || windows.size() > kMaxWindows) {
            break;
        }
        windows.push_back({range.tempo.framesToTicks(frame, range.sampleRate), frame});
    }
    return windows;
}

std::vector<WindowStart> windowStarts(const AnalysisRange& range, const Json& resolution) {
    std::vector<WindowStart> windows;
    if (resolution.is_number()) {
        windows = secondWindows(range, resolution.get<double>());
    } else if (resolution.is_string()) {
        const std::size_t choice = choiceIndex("resolution", kResolutionNames, resolution.get<std::string>(),
                                               "give bar, beat or a number of seconds");
        windows = tickWindows(range, std::invoke(kResolutionSteps.at(choice), range.tempo));
    } else {
        throw AppError("give resolution as bar, beat or a number of seconds, not " + resolution.dump());
    }
    if (windows.size() > kMaxWindows) {
        throw AppError(std::format("the range holds more than {} windows, give a coarser resolution or a shorter range",
                                   kMaxWindows));
    }
    return windows;
}

Json toneBandsJson(const engine::ToneBalance& tone) {
    return std::views::zip(engine::kToneBands, tone.bandShare) | std::views::transform([](const auto& entry) {
               const auto& [band, share] = entry;
               return Json{{"name", std::string{band.name}},
                           {"lowHz", band.low},
                           {"highHz", std::isfinite(band.high) ? Json(band.high) : Json(nullptr)},
                           {"share", std::round(share * 1000) / 1000}};
           }) |
           std::ranges::to<Json::array_t>();
}

Json windowPosition(const AnalysisRange& range, core::Ticks tick) {
    return {{"bbt", range.tempo.toBarBeatTick(tick).format()},
            {"seconds", rounded(range.tempo.ticksToSeconds(tick), 0.001)}};
}

Json windowJson(const AnalysisRange& range, std::size_t index, const engine::WindowLevels& window) {
    Json result{{"at", windowPosition(range, range.windows[index].tick)},
                {"activeRmsDb", nullptr},
                {"peakDb", levelDb(window.peakDb)},
                {"centroidHz", nullptr},
                {"bands", nullptr},
                {"tonalPeakHz", nullptr},
                {"tonality", nullptr},
                {"crestDb", nullptr},
                {"clipped", window.clipped}};
    if (!std::isfinite(window.activeRmsDb)) {
        return result;
    }
    result["activeRmsDb"] = levelDb(window.activeRmsDb);
    result["centroidHz"] = std::round(window.centroidHz);
    result["bands"] = window.bandShare | std::views::transform([](double share) { return rounded(share, 0.01); }) |
                      std::ranges::to<Json::array_t>();
    result["crestDb"] = levelDb(window.crestDb);
    if (window.tonalPeakHz > 0) {
        result["tonalPeakHz"] = std::round(window.tonalPeakHz);
        result["tonality"] = rounded(window.tonality, 0.01);
    }
    return result;
}

struct Measured {
    engine::TrackLevels levels;
    std::vector<engine::WindowLevels> windows;
};

Json levelsJson(const AnalysisRange& range, const std::string& trackId, const Measured& measured) {
    const engine::TrackLevels& levels = measured.levels;
    Json result{{"track", trackId},
                {"from", positionState(range.from, range.tempo)},
                {"to", positionState(range.to, range.tempo)},
                {"integratedLufs", levelDb(levels.integratedLufs)},
                {"samplePeakDbfs", levelDb(levels.samplePeakDbfs)},
                {"truePeakDbtp", levelDb(levels.truePeakDbtp)},
                {"activeRmsDb", levelDb(levels.tone.activeRmsDb)},
                {"bands", toneBandsJson(levels.tone)},
                {"centroidHz", std::round(levels.tone.centroidHz)},
                {"message", loudnessMessage(levels.integratedLufs, levels.truePeakDbtp,
                                            std::format("{:.1f} dB active RMS", levels.tone.activeRmsDb))}};
    if (!range.windows.empty()) {
        result["windows"] = std::views::iota(std::size_t{0}, measured.windows.size()) |
                            std::views::transform(
                                [&](std::size_t index) { return windowJson(range, index, measured.windows[index]); }) |
                            std::ranges::to<Json::array_t>();
    }
    return result;
}

// The meters of each output a render carries on channels 2i and 2i + 1, fed from the frame the range starts at.
struct OutputMeters {
    std::ptrdiff_t skip = 0;
    std::ptrdiff_t seen = 0;
    std::vector<engine::TrackMeter> meters;
    std::vector<engine::WindowMeter> windows;

    void add(const std::vector<std::vector<float>>& chunk) {
        const std::ptrdiff_t length = std::ssize(chunk.front());
        const std::ptrdiff_t first = std::clamp(skip - seen, std::ptrdiff_t{0}, length);
        seen += length;
        if (first == length) {
            return;
        }
        // Each output's meters run on their own thread, since measuring takes about as long as rendering.
        std::vector<std::future<void>> measured;
        measured.reserve(meters.size());
        for (std::size_t index = 0; index < meters.size(); ++index) {
            measured.push_back(std::async(std::launch::async, [this, &chunk, first, index] {
                const auto channels = std::span{chunk}.subspan(2 * index, 2);
                const auto measure = [&](std::span<const std::vector<float>> part) {
                    meters[index].add(part);
                    if (!windows.empty()) {
                        windows[index].add(part);
                    }
                };
                if (first == 0) {
                    measure(channels);
                    return;
                }
                measure(channels | std::views::transform([first](const std::vector<float>& channel) {
                            return std::vector<float>(channel.begin() + first, channel.end());
                        }) |
                        std::ranges::to<std::vector>());
            }));
        }
        for (std::future<void>& meter : measured) {
            meter.get();
        }
    }

    [[nodiscard]] std::vector<Measured> results() const {
        return std::views::iota(std::size_t{0}, meters.size()) | std::views::transform([&](std::size_t index) {
                   return Measured{
                       .levels = meters[index].levels(),
                       .windows = windows.empty() ? std::vector<engine::WindowLevels>{} : windows[index].windows()};
               }) |
               std::ranges::to<std::vector>();
    }
};

struct Measurement {
    std::vector<std::string> trackIds;
    // With the mix, output 0 carries it and output i + 1 the i-th track.
    bool withMix = false;
    // We measure no windows when this and the params' resolution are both null.
    Json defaultResolution;
    std::function<Json(const AnalysisRange&, const engine::Session&, std::vector<Measured>)> report;
};

// UI thread: checks from, to and resolution and sets up one render of the outputs, each track as its stem plays, and
// what report makes of their measurements.
RenderJob prepareMeasurement(ProjectController& controller, const Registry& registry, const Json& params,
                             Measurement measurement) {
    const model::Document document = liveDocument(controller);
    auto session = std::make_shared<const engine::Session>(toSession(document, registry, controller.folder()));
    const core::TempoMap& tempo = document.tempo;
    const core::Ticks end = exportEnd(*session, document);
    if (end <= 0) {
        throw AppError("the project has no clips to analyze, add a clip first");
    }
    AnalysisRange range;
    range.tempo = tempo;
    range.from = optionalPosition(params, "from", tempo).value_or(0);
    range.to = std::min(optionalPosition(params, "to", tempo).value_or(end), end);
    range.sampleRate = controller.sampleRate();
    if (range.from < 0 || range.from >= range.to) {
        throw AppError(std::format("give from before to, both from {} to the project end {}",
                                   tempo.toBarBeatTick(0).format(), tempo.toBarBeatTick(end).format()));
    }
    const Json resolution = params.contains("resolution") && !params.at("resolution").is_null()
                                ? params.at("resolution")
                                : measurement.defaultResolution;
    if (!resolution.is_null()) {
        range.windows = windowStarts(range, resolution);
    }
    const std::vector<std::string>& trackIds = measurement.trackIds;
    const PlaybackContext context = playbackContext(controller, document);
    const std::size_t outputs = trackIds.size() + (measurement.withMix ? 1 : 0);
    auto meters = std::make_shared<OutputMeters>();
    meters->skip = range.frameOf(range.from);
    meters->meters = std::views::iota(std::size_t{0}, outputs) |
                     std::views::transform([&](std::size_t) { return engine::TrackMeter(2, range.sampleRate); }) |
                     std::ranges::to<std::vector>();
    if (!range.windows.empty()) {
        const std::vector<std::size_t> ends =
            std::views::iota(std::size_t{0}, range.windows.size()) | std::views::transform([&](std::size_t index) {
                return static_cast<std::size_t>(range.windowEnd(index) - meters->skip);
            }) |
            std::ranges::to<std::vector>();
        meters->windows =
            std::views::iota(std::size_t{0}, outputs) |
            std::views::transform([&](std::size_t) { return engine::WindowMeter(2, range.sampleRate, ends); }) |
            std::ranges::to<std::vector>();
    }
    std::unique_ptr<OfflineRender> render;
    if (measurement.withMix) {
        render = mixAndTracksRender(*session, registry, context, trackIds, range.sampleRate);
    } else if (trackIds.size() == 1) {
        render = trackRender(*session, registry, context, trackIds.front(), range.sampleRate);
    } else {
        render = tracksRender(*session, registry, context, trackIds, range.sampleRate);
    }
    return {.render = std::move(render),
            .frames = range.frameOf(range.to),
            .sink = [meters](const std::vector<std::vector<float>>& chunk) { meters->add(chunk); },
            .finish =
                [meters, range, session, report = std::move(measurement.report)] {
                    return RenderResult{.channels = {}, .json = report(range, *session, meters->results())};
                }};
}

std::vector<std::string> trackIdsOf(const std::vector<model::TrackId>& ids) {
    return ids | std::views::transform(&model::TrackId::value) | std::ranges::to<std::vector>();
}

RenderJob prepareAnalysis(ProjectController& controller, const Registry& registry, const Json& params,
                          const std::vector<model::TrackId>& ids) {
    return prepareMeasurement(
        controller, registry, params,
        {.trackIds = trackIdsOf(ids),
         .withMix = false,
         .defaultResolution = nullptr,
         .report = [trackIds = trackIdsOf(ids)](const AnalysisRange& range, const engine::Session&,
                                                std::vector<Measured> measured) {
             Json json = Json::object();
             for (const auto& [id, levels] : std::views::zip(trackIds, measured)) {
                 json[id] = levelsJson(range, id, levels);
             }
             return json;
         }});
}

engine::InspectedOutput trackOutput(const engine::Session& session, const std::string& id, Measured measured) {
    engine::InspectedOutput output{.levels = measured.levels, .windows = std::move(measured.windows)};
    if (const auto track = std::ranges::find(session.tracks, id, &engine::Track::id); track != session.tracks.end()) {
        output.role = track->kind == engine::TrackKind::bus ? engine::OutputRole::bus : engine::OutputRole::track;
        output.hasClips = !track->audioClips.empty() || !track->midiClips.empty();
    }
    return output;
}

Json issueJson(const AnalysisRange& range, const std::vector<std::string>& outputIds, const engine::MixIssue& issue) {
    Json evidence = Json::object();
    for (const engine::IssueEvidence& entry : issue.evidence) {
        evidence[std::string{entry.name}] = rounded(entry.value, 0.01);
    }
    return {
        {"severity", engine::kIssueSeverityNames.at(static_cast<std::size_t>(issue.severity))},
        {"kind", engine::kIssueKindNames.at(static_cast<std::size_t>(issue.kind))},
        {"track", outputIds.at(issue.output)},
        {"from", windowPosition(range, range.windows.empty() ? range.from : range.windows.at(issue.firstWindow).tick)},
        {"to", windowPosition(range, range.windowEndTick(issue.lastWindow))},
        {"detail", issue.detail},
        {"evidence", evidence}};
}

Json inspectionJson(const AnalysisRange& range, const engine::Session& session,
                    const std::vector<std::string>& trackIds, std::vector<Measured> measured) {
    std::vector<std::string> outputIds{kMixId};
    outputIds.insert(outputIds.end(), trackIds.begin(), trackIds.end());
    const std::vector<engine::InspectedOutput> outputs =
        std::views::iota(std::size_t{0}, measured.size()) | std::views::transform([&](std::size_t index) {
            if (index == 0) {
                return engine::InspectedOutput{.role = engine::OutputRole::mix,
                                               .levels = measured[index].levels,
                                               .windows = std::move(measured[index].windows)};
            }
            return trackOutput(session, outputIds[index], std::move(measured[index]));
        }) |
        std::ranges::to<std::vector>();
    const std::vector<double> beats =
        std::views::iota(std::size_t{0}, range.windows.size()) |
        std::views::transform([&](std::size_t index) { return range.windowBeats(index); }) |
        std::ranges::to<std::vector>();
    const Json issues =
        engine::inspectMix(outputs, beats) |
        std::views::transform([&](const engine::MixIssue& issue) { return issueJson(range, outputIds, issue); }) |
        std::ranges::to<Json::array_t>();
    return {{"message", std::format("{} issues in the mix and {} tracks", issues.size(), trackIds.size())},
            {"from", positionState(range.from, range.tempo)},
            {"to", positionState(range.to, range.tempo)},
            {"issues", issues}};
}

}  // namespace

std::string loudnessMessage(double integratedLufs, double truePeakDbtp, const std::string& detail) {
    return std::format("{:.1f} LUFS integrated, {:.1f} dBTP true peak, {}", integratedLufs, truePeakDbtp, detail);
}

RenderJob prepareTrackRender(ProjectController& controller, const Registry& registry, const Json& params) {
    const model::TrackId id = findTrackByIdOrName(controller.project(), param<std::string>(params, "track"));
    const model::Track& source = *controller.project().track(id);
    if (const TrackType* type = registry.trackType(source.kind); type == nullptr || !playsAudio(*type, source)) {
        throw AppError(playsNoAudio(type, source, "pick an audio track or a bus"));
    }
    return prepareAnalysis(controller, registry, params, {id});
}

RenderJob prepareTracksRender(ProjectController& controller, const Registry& registry, const Json& params) {
    const std::vector<model::TrackId> ids = analyzableTracks(registry, controller.project().document());
    if (ids.empty()) {
        throw AppError("the project has no audio tracks or buses to analyze, add one first");
    }
    return prepareAnalysis(controller, registry, params, ids);
}

RenderJob prepareMixInspection(ProjectController& controller, const Registry& registry, const Json& params) {
    const std::vector<model::TrackId> ids = analyzableTracks(registry, controller.project().document());
    if (ids.empty()) {
        throw AppError("the project has no audio tracks or buses to inspect, add one first");
    }
    return prepareMeasurement(
        controller, registry, params,
        {.trackIds = trackIdsOf(ids),
         .withMix = true,
         .defaultResolution = kResolutionNames.front(),
         .report = [trackIds = trackIdsOf(ids)](const AnalysisRange& range, const engine::Session& session,
                                                std::vector<Measured> measured) {
             return inspectionJson(range, session, trackIds, std::move(measured));
         }});
}

}  // namespace emerick::app
