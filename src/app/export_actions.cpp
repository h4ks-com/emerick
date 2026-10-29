#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/audio_file.hpp"
#include "emerick/app/midi_export.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/app/stem_export.hpp"
#include "emerick/engine/audio_analysis.hpp"

namespace emerick::app {

namespace {

// The project with each instrument's state as it plays now, which an export renders with fresh instances.
model::Document liveDocument(ProjectController& controller) {
    return controller.instruments().withStates(controller.project().document());
}

PlaybackContext playbackContext(ProjectController& controller, const model::Document& document) {
    return {.tempo = document.tempo, .folder = controller.folder(), .document = &document};
}

// Listed tracks always export, with the tracks that send them MIDI when midiSources is set; without a list, skipMuted
// leaves the muted ones out.
engine::Session exportSession(ProjectController& controller, const model::Document& document, const Registry& registry,
                              const Json& params, bool skipMuted, bool midiSources = false) {
    engine::Session session = toSession(document, registry, controller.folder());
    std::vector<std::string> ids;
    if (const auto requested = optionalParam<std::vector<std::string>>(params, "tracks")) {
        ids = *requested | std::views::transform([&](const std::string& track) {
            return findTrackByIdOrName(controller.project(), track).value;
        }) | std::ranges::to<std::vector>();
        for (std::size_t index = 0; midiSources && index < ids.size(); ++index) {
            const auto track = std::ranges::find(session.tracks, ids[index], &engine::Track::id);
            if (track != session.tracks.end() && !track->midiInTrack.empty() &&
                !std::ranges::contains(ids, track->midiInTrack)) {
                ids.push_back(track->midiInTrack);
            }
        }
    } else {
        ids = document.tracks |
              std::views::filter([&](const model::Track& track) { return !skipMuted || !track.mute; }) |
              std::views::transform([](const model::Track& track) { return track.id.value; }) |
              std::ranges::to<std::vector>();
    }
    std::erase_if(session.tracks,
                  [&](const engine::Track& track) { return std::ranges::find(ids, track.id) == ids.end(); });
    return session;
}

Json fileList(const std::vector<std::filesystem::path>& files) {
    return files | std::views::transform([](const std::filesystem::path& file) { return file.string(); }) |
           std::ranges::to<Json::array_t>();
}

// Stems carry track numbers, so we delete an earlier export's WAVs and MIDI file, or a reordered project would leave
// stale stems beside the new ones.
void removeEarlierExport(const std::filesystem::path& folder) {
    if (!std::filesystem::is_directory(folder)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file() && (entry.path().extension() == ".wav" || entry.path().filename() == "MIDI.mid")) {
            std::filesystem::remove(entry.path());
        }
    }
}

// Logic takes MIDI tracks from the MIDI file, so only audio tracks and buses render as stems.
std::vector<std::filesystem::path> exportForLogic(ProjectController& controller, const Registry& registry,
                                                  const model::Document& document, const engine::Session& session,
                                                  const std::filesystem::path& folder) {
    removeEarlierExport(folder);
    std::vector<std::filesystem::path> files;
    const bool hasAudio = std::ranges::any_of(
        session.tracks, [](const engine::Track& track) { return track.kind == engine::TrackKind::audio; });
    if (hasAudio && projectEnd(document) > 0) {
        engine::Session audio = session;
        std::erase_if(audio.tracks, [](const engine::Track& track) { return track.kind == engine::TrackKind::midi; });
        files =
            exportStems(audio, registry, playbackContext(controller, document), folder, controller.sampleRate(), true);
    }
    files.push_back(folder / "MIDI.mid");
    exportMidi(session, document, registry, controller.name().toStdString(), files.back());
    return files;
}

double rounded(double value, double step) { return std::round(value / step) * step; }

Json levelDb(double value) { return std::isfinite(value) ? Json(rounded(value, 0.1)) : Json(nullptr); }

Json analysisJson(const engine::AudioAnalysis& analysis) {
    Json bands = Json::object();
    for (const auto& [band, db] : std::views::zip(engine::kFrequencyBands, analysis.bandDb)) {
        bands[std::string{band.name}] = levelDb(db);
    }
    Json result{{"integratedLufs", levelDb(analysis.integratedLufs)},
                {"truePeakDbtp", levelDb(analysis.truePeakDbtp)},
                {"samplePeakDbfs", levelDb(analysis.samplePeakDbfs)},
                {"bars", analysis.bars | std::views::transform([](const engine::BarLevel& bar) {
                             return Json{{"bar", bar.bar}, {"rmsDb", levelDb(bar.rmsDb)}};
                         }) | std::ranges::to<Json::array_t>()},
                {"bandsDb", bands},
                {"silence", analysis.silence | std::views::transform([](const engine::SilentRegion& region) {
                                return Json{rounded(region.start, 0.01), rounded(region.end, 0.01)};
                            }) | std::ranges::to<Json::array_t>()},
                {"clippedSamples", analysis.clippedSamples}};
    if (analysis.stereo) {
        result["stereo"] = {{"correlation", rounded(analysis.stereo->correlation, 0.01)},
                            {"balanceDb", levelDb(analysis.stereo->balanceDb)}};
    }
    return result;
}

// Bars follow the project tempo and meter even for a file from outside the project.
Json analyzeSection(const AudioData& audio, const Json& params, const core::TempoMap& tempo) {
    const std::size_t frames = audio.channels.empty() ? 0 : audio.channels.front().size();
    const double seconds = static_cast<double>(frames) / audio.sampleRate;
    const double from = optionalParam<double>(params, "from").value_or(0);
    const double to = std::min(optionalParam<double>(params, "to").value_or(seconds), seconds);
    if (from < 0 || from >= to) {
        throw AppError(
            std::format("give from and to in seconds with 0 <= from < to <= {:.2f}, the audio length", seconds));
    }
    const auto first = static_cast<std::ptrdiff_t>(std::lround(from * audio.sampleRate));
    const auto last = static_cast<std::ptrdiff_t>(std::lround(to * audio.sampleRate));
    const auto section = audio.channels | std::views::transform([&](const std::vector<float>& channel) {
                             return std::vector<float>(channel.begin() + first, channel.begin() + last);
                         }) |
                         std::ranges::to<std::vector>();
    const engine::AudioAnalysis analysis =
        engine::analyzeAudio(section, {.sampleRate = audio.sampleRate,
                                       .barSeconds = tempo.ticksToSeconds(tempo.ticksPerBar()),
                                       .startSeconds = from});
    Json result = analysisJson(analysis);
    result["message"] = std::format("{:.1f} LUFS integrated, {:.1f} dBTP true peak, {} clipped samples",
                                    analysis.integratedLufs, analysis.truePeakDbtp, analysis.clippedSamples);
    result["from"] = rounded(from, 0.01);
    result["to"] = rounded(to, 0.01);
    result["channels"] = audio.channels.size();
    result["sampleRate"] = audio.sampleRate;
    return result;
}

}  // namespace

void registerExportActions(Registry& registry, ProjectController& controller) {
    const Json tracksSchema{{"tracks", {{"type", "array"}}}};
    registry.addAction({.id = "export.stems",
                        .title = "Export stems",
                        .aliases = {"bounce tracks", "render stems"},
                        .category = "Export",
                        .schema = {{"required", {"folder"}}, {"properties", tracksSchema}},
                        .run =
                            [&controller, &registry](const Json& params) {
                                const std::filesystem::path folder =
                                    registry.checkedPath(param<std::string>(params, "folder"), PathAccess::Write);
                                const model::Document document = liveDocument(controller);
                                const engine::Session session =
                                    exportSession(controller, document, registry, params, false, true);
                                const auto files = exportStems(session, registry, playbackContext(controller, document),
                                                               folder, controller.sampleRate());
                                return Json{{"files", fileList(files)},
                                            {"end", positionState(exportEnd(session, document), document.tempo)}};
                            },
                        .menus = {kFileMenu},
                        .dialog = FileDialog{.param = "folder", .mode = FileDialogMode::Folder}});
    registry.addAction(
        {.id = "export.mix",
         .title = "Export mix",
         .aliases = {"bounce mix", "render mix"},
         .category = "Export",
         .schema = {{"required", {"path"}}},
         .run =
             [&controller, &registry](const Json& params) {
                 const std::filesystem::path file =
                     registry.checkedPath(param<std::string>(params, "path"), PathAccess::Write);
                 const model::Document document = liveDocument(controller);
                 exportMix(toSession(document, registry, controller.folder()), registry,
                           playbackContext(controller, document), file, controller.sampleRate());
                 return Json{{"file", file.string()}};
             },
         .menus = {kFileMenu},
         .dialog = FileDialog{
             .param = "path", .mode = FileDialogMode::Save, .nameFilter = "WAV files (*.wav)", .suffix = "wav"}});
    registry.addAction({.id = "export.logic",
                        .title = "Export for Logic",
                        .aliases = {"logic pro", "send to logic"},
                        .keywords = {"stems", "midi file", "markers"},
                        .category = "Export",
                        .schema = {{"required", {"folder"}}, {"properties", tracksSchema}},
                        .run =
                            [&controller, &registry](const Json& params) {
                                const std::filesystem::path folder =
                                    registry.checkedPath((std::filesystem::path{param<std::string>(params, "folder")} /
                                                          (controller.name().toStdString() + " for Logic"))
                                                             .string(),
                                                         PathAccess::Write);
                                const model::Document document = liveDocument(controller);
                                const auto files =
                                    exportForLogic(controller, registry, document,
                                                   exportSession(controller, document, registry, params, true), folder);
                                return Json{{"folder", folder.string()}, {"files", fileList(files)}};
                            },
                        .menus = {kFileMenu, kProjectMenu},
                        .dialog = FileDialog{.param = "folder", .mode = FileDialogMode::Folder}});
    registry.addAction(
        {.id = "export.midi",
         .title = "Export MIDI file",
         .aliases = {"save midi", "standard midi file"},
         .category = "Export",
         .schema = {{"required", {"path"}}, {"properties", tracksSchema}},
         .run =
             [&controller, &registry](const Json& params) {
                 const std::filesystem::path file =
                     registry.checkedPath(param<std::string>(params, "path"), PathAccess::Write);
                 const model::Document& document = controller.project().document();
                 exportMidi(exportSession(controller, document, registry, params, true), document, registry,
                            controller.name().toStdString(), file);
                 return Json{{"file", file.string()}};
             },
         .menus = {kFileMenu},
         .dialog = FileDialog{
             .param = "path", .mode = FileDialogMode::Save, .nameFilter = "MIDI files (*.mid)", .suffix = "mid"}});
    const Json sectionSchema{{"from", {{"type", "number"}}}, {"to", {{"type", "number"}}}};
    Json fileSchema = sectionSchema;
    fileSchema["path"] = {{"type", "string"}};
    registry.addAction({.id = "audio.analyze",
                        .title = "Analyze audio file",
                        .description = "Measure loudness, peaks, bar levels, bands, silence, clipping and stereo image "
                                       "of a WAV or AIFF file, optionally from and to a time in seconds.",
                        .keywords = {"loudness", "lufs", "true peak", "meter"},
                        .category = "Analyze",
                        .schema = {{"required", {"path"}}, {"properties", fileSchema}},
                        .run =
                            [&controller, &registry](const Json& params) {
                                const std::filesystem::path file =
                                    registry.checkedPath(param<std::string>(params, "path"), PathAccess::Read);
                                return analyzeSection(readAudio(file), params, controller.project().document().tempo);
                            },
                        .menus = {kFileMenu},
                        .dialog = FileDialog{.param = "path",
                                             .mode = FileDialogMode::Open,
                                             .nameFilter = "Audio files (*.wav *.aif *.aiff)"},
                        .notifies = true});
    registry.addAction(
        {.id = "render.analyze",
         .title = "Analyze mix",
         .description = "Render the mix as Export mix does and measure loudness, peaks, bar levels, bands, silence, "
                        "clipping and stereo image, optionally from and to a time in seconds.",
         .keywords = {"loudness", "lufs", "true peak", "meter", "bounce"},
         .category = "Analyze",
         .schema = {{"properties", sectionSchema}},
         .run =
             [&controller, &registry](const Json& params) {
                 const QTemporaryDir folder;
                 if (!folder.isValid()) {
                     throw AppError("cannot render: no temporary folder: " + folder.errorString().toStdString());
                 }
                 const std::filesystem::path file = std::filesystem::path{folder.path().toStdString()} / "mix.wav";
                 const model::Document document = liveDocument(controller);
                 exportMix(toSession(document, registry, controller.folder()), registry,
                           playbackContext(controller, document), file, controller.sampleRate());
                 return analyzeSection(readAudio(file), params, document.tempo);
             },
         .menus = {kFileMenu},
         .notifies = true});
}

}  // namespace emerick::app
