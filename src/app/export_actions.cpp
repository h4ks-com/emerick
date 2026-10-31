#include <QImage>
#include <QSize>
#include <QString>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/audio_file.hpp"
#include "emerick/app/audio_image.hpp"
#include "emerick/app/midi_export.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/app/stem_export.hpp"
#include "emerick/app/track_analysis.hpp"
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

// A render result carries the track warnings, since a warned track can render silent or with stand-in sounds.
Json withWarnings(Json result, const ProjectController& controller) {
    const QVariantMap warnings = controller.trackErrors();
    for (const auto& [track, text] : warnings.asKeyValueRange()) {
        result["warnings"][track.toStdString()] = text.toString().toStdString();
    }
    return result;
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

std::string loudnessMessage(const engine::AudioAnalysis& analysis, const std::string& detail) {
    return std::format("{:.1f} LUFS integrated, {:.1f} dBTP true peak, {}", analysis.integratedLufs,
                       analysis.truePeakDbtp, detail);
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

Json analyzeTrackRender(const TrackRender& job) {
    OfflineRender& render = *job.render;
    return measureTrack(job.range, render.process(job.range.endFrame(), [&render] { render.idleInstruments(); }));
}

struct AudioSection {
    AudioData audio;
    double from = 0;
    double to = 0;
};

AudioSection audioSection(const AudioData& audio, const Json& params) {
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
    return {.audio = {.channels = audio.channels | std::views::transform([&](const std::vector<float>& channel) {
                                      return std::vector<float>(channel.begin() + first, channel.begin() + last);
                                  }) |
                                  std::ranges::to<std::vector>(),
                      .sampleRate = audio.sampleRate},
            .from = from,
            .to = to};
}

// Bars follow the project tempo and meter even for a file from outside the project.
double barSeconds(const core::TempoMap& tempo) { return tempo.ticksToSeconds(tempo.ticksPerBar()); }

Json analyzeSection(const AudioData& audio, const Json& params, const core::TempoMap& tempo) {
    const AudioSection section = audioSection(audio, params);
    const engine::AudioAnalysis analysis = engine::analyzeAudio(
        section.audio.channels,
        {.sampleRate = audio.sampleRate, .barSeconds = barSeconds(tempo), .startSeconds = section.from});
    Json result = analysisJson(analysis);
    result["message"] = loudnessMessage(analysis, std::format("{} clipped samples", analysis.clippedSamples));
    result["from"] = rounded(section.from, 0.01);
    result["to"] = rounded(section.to, 0.01);
    result["channels"] = audio.channels.size();
    result["sampleRate"] = audio.sampleRate;
    return result;
}

enum class AudioImageKind : std::uint8_t { Waveform, Spectrogram };

constexpr std::array<std::pair<std::string_view, AudioImageKind>, 2> kAudioImageKinds{
    {{"waveform", AudioImageKind::Waveform}, {"spectrogram", AudioImageKind::Spectrogram}}};

constexpr int kMaxImageSide = 8192;

int imageSide(const Json& params, const std::string& key, int fallback) {
    const int side = optionalParam<int>(params, key).value_or(fallback);
    if (side < 1 || side > kMaxImageSide) {
        throw AppError(std::format("give {} in pixels from 1 to {}", key, kMaxImageSide));
    }
    return side;
}

Json writeAudioImage(const AudioData& audio, const Json& params, const std::filesystem::path& out,
                     const core::TempoMap& tempo) {
    const std::string kindName = optionalParam<std::string>(params, "kind").value_or("waveform");
    const auto* kind =
        std::ranges::find(kAudioImageKinds, kindName, &std::pair<std::string_view, AudioImageKind>::first);
    if (kind == kAudioImageKinds.end()) {
        throw AppError("kind must be waveform or spectrogram, not " + kindName);
    }
    const QSize size{imageSide(params, "width", 1200), imageSide(params, "height", 300)};
    const AudioSection section = audioSection(audio, params);
    const QImage image = kind->second == AudioImageKind::Waveform
                             ? waveformImage(section.audio, size, section.from, barSeconds(tempo))
                             : spectrogramImage(section.audio, size);
    if (!image.save(QString::fromStdString(out.string()), "PNG")) {
        throw AppError("cannot write " + out.string() + " as PNG");
    }
    return {{"path", out.string()},
            {"width", size.width()},
            {"height", size.height()},
            {"seconds", rounded(section.to - section.from, 0.01)},
            {"kind", kindName}};
}

constexpr const char* kNormalizeSetting = "export.normalize";
constexpr const char* kTargetLufsSetting = "export.targetLufs";
constexpr const char* kCeilingSetting = "export.ceilingDbtp";
constexpr double kMinTargetLufs = -60;
constexpr double kMinCeilingDbtp = -12;

double boundedParam(const Json& params, const Registry& registry, const char* key, const char* setting,
                    double minimum) {
    const double value = optionalParam<double>(params, key).value_or(registry.settingValue(setting).get<double>());
    if (value < minimum || value > 0) {
        throw AppError(std::format("give {} from {} to 0", key, minimum));
    }
    return value;
}

std::optional<LoudnessTarget> loudnessTarget(const Registry& registry, const Json& params) {
    const bool normalize = optionalParam<bool>(params, "normalize")
                               .value_or(params.contains("targetLufs") || params.contains("ceilingDbtp") ||
                                         registry.settingValue(kNormalizeSetting).get<bool>());
    if (!normalize) {
        return std::nullopt;
    }
    return LoudnessTarget{
        .lufs = boundedParam(params, registry, "targetLufs", kTargetLufsSetting, kMinTargetLufs),
        .ceilingDbtp = boundedParam(params, registry, "ceilingDbtp", kCeilingSetting, kMinCeilingDbtp)};
}

void registerExportSettings(Registry& registry) {
    registry.addSetting({.id = kNormalizeSetting,
                         .title = "Normalize mix exports",
                         .aliases = {"loudness target", "lufs"},
                         .category = "Settings / Audio",
                         .type = SettingType::Boolean,
                         .defaultValue = false,
                         .hint = "Turn exported mixes to the target loudness under the true peak ceiling",
                         .location = "settings.audio"});
    registry.addSetting({.id = kTargetLufsSetting,
                         .title = "Export loudness target",
                         .category = "Settings / Audio",
                         .type = SettingType::Number,
                         .defaultValue = -14,
                         .minimum = kMinTargetLufs,
                         .maximum = 0,
                         .hint = "Integrated LUFS a normalized mix export ends at",
                         .location = "settings.audio"});
    registry.addSetting({.id = kCeilingSetting,
                         .title = "Export true peak ceiling",
                         .category = "Settings / Audio",
                         .type = SettingType::Number,
                         .defaultValue = -1,
                         .minimum = kMinCeilingDbtp,
                         .maximum = 0,
                         .hint = "dBTP a normalized mix export never goes over",
                         .location = "settings.audio"});
}

}  // namespace

TrackRender prepareTrackRender(ProjectController& controller, const Registry& registry, const Json& params) {
    const model::Document document = liveDocument(controller);
    const model::TrackId id = findTrackByIdOrName(controller.project(), param<std::string>(params, "track"));
    const model::Track& source = *model::findTrack(document, id);
    if (const TrackType* type = registry.trackType(source.kind); type == nullptr || !playsAudio(*type, source)) {
        throw AppError(playsNoAudio(type, source, "pick an audio track or a bus"));
    }
    const engine::Session session = toSession(document, registry, controller.folder());
    const core::TempoMap& tempo = document.tempo;
    const core::Ticks end = exportEnd(session, document);
    if (end <= 0) {
        throw AppError("the project has no clips to analyze, add a clip first");
    }
    AnalysisRange range;
    range.tempo = tempo;
    range.trackId = id.value;
    range.from = optionalPosition(params, "from", tempo).value_or(0);
    range.to = std::min(optionalPosition(params, "to", tempo).value_or(end), end);
    range.sampleRate = controller.sampleRate();
    if (range.from < 0 || range.from >= range.to) {
        throw AppError(std::format("give from before to, both from {} to the project end {}",
                                   tempo.toBarBeatTick(0).format(), tempo.toBarBeatTick(end).format()));
    }
    return {.render =
                trackRender(session, registry, playbackContext(controller, document), range.trackId, range.sampleRate),
            .range = std::move(range)};
}

Json measureTrack(const AnalysisRange& range, std::vector<std::vector<float>> channels) {
    const auto skipped = static_cast<std::ptrdiff_t>(range.tempo.ticksToFrames(range.from, range.sampleRate));
    for (std::vector<float>& channel : channels) {
        channel.erase(channel.begin(), channel.begin() + std::min(skipped, std::ssize(channel)));
    }
    const engine::AudioAnalysis levels = engine::analyzeAudio(channels, {.sampleRate = range.sampleRate});
    const engine::ToneBalance tone = engine::analyzeTone(channels, range.sampleRate);
    return {{"track", range.trackId},
            {"from", positionState(range.from, range.tempo)},
            {"to", positionState(range.to, range.tempo)},
            {"integratedLufs", levelDb(levels.integratedLufs)},
            {"samplePeakDbfs", levelDb(levels.samplePeakDbfs)},
            {"truePeakDbtp", levelDb(levels.truePeakDbtp)},
            {"activeRmsDb", levelDb(tone.activeRmsDb)},
            {"bands", toneBandsJson(tone)},
            {"centroidHz", std::round(tone.centroidHz)},
            {"message", loudnessMessage(levels, std::format("{:.1f} dB active RMS", tone.activeRmsDb))}};
}

void registerExportActions(Registry& registry, ProjectController& controller) {
    registerExportSettings(registry);
    const Json tracksSchema{{"tracks", {{"type", "array"}}}};
    registry.addAction(
        {.id = "export.stems",
         .title = "Export stems",
         .aliases = {"bounce tracks", "render stems"},
         .category = "Export",
         .schema = {{"required", {"folder"}}, {"properties", tracksSchema}},
         .run =
             [&controller, &registry](const Json& params) {
                 const std::filesystem::path folder =
                     registry.checkedPath(param<std::string>(params, "folder"), PathAccess::Write);
                 const model::Document document = liveDocument(controller);
                 const engine::Session session = exportSession(controller, document, registry, params, false, true);
                 const auto files = exportStems(session, registry, playbackContext(controller, document), folder,
                                                controller.sampleRate());
                 return withWarnings(Json{{"files", fileList(files)},
                                          {"end", positionState(exportEnd(session, document), document.tempo)}},
                                     controller);
             },
         .menus = {kFileMenu},
         .dialog = FileDialog{.param = "folder", .mode = FileDialogMode::Folder}});
    registry.addAction(
        {.id = "export.mix",
         .title = "Export mix",
         .description = "Render the mix to a stereo WAV at path. With normalize, or when targetLufs or ceilingDbtp is "
                        "given, turn the mix to targetLufs integrated loudness and limit its true peak to "
                        "ceilingDbtp, and report the integratedLufs and truePeakDbtp the file ends at. Each one "
                        "defaults to its export setting.",
         .aliases = {"bounce mix", "render mix"},
         .keywords = {"normalize", "loudness", "lufs", "true peak", "limiter"},
         .category = "Export",
         .schema = {{"required", {"path"}},
                    {"properties",
                     {{"path", {{"type", "string"}}},
                      {"normalize", {{"type", "boolean"}}},
                      {"targetLufs", {{"type", "number"}, {"minimum", kMinTargetLufs}, {"maximum", 0}}},
                      {"ceilingDbtp", {{"type", "number"}, {"minimum", kMinCeilingDbtp}, {"maximum", 0}}}}}},
         .run =
             [&controller, &registry](const Json& params) {
                 const std::filesystem::path file =
                     registry.checkedPath(param<std::string>(params, "path"), PathAccess::Write);
                 const model::Document document = liveDocument(controller);
                 const std::optional<MixLevels> levels =
                     exportMix(toSession(document, registry, controller.folder()), registry,
                               playbackContext(controller, document), file, controller.sampleRate(),
                               loudnessTarget(registry, params));
                 Json result{{"file", file.string()}};
                 if (levels) {
                     result["integratedLufs"] = levelDb(levels->integratedLufs);
                     result["truePeakDbtp"] = levelDb(levels->truePeakDbtp);
                 }
                 return withWarnings(result, controller);
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
    Json imageSchema = fileSchema;
    imageSchema["out"] = {{"type", "string"}};
    imageSchema["kind"] = {{"type", "string"},
                           {"enum", kAudioImageKinds | std::views::keys | std::ranges::to<std::vector<std::string>>()}};
    imageSchema["width"] = {{"type", "integer"}};
    imageSchema["height"] = {{"type", "integer"}};
    registry.addAction(
        {.id = "audio.image",
         .title = "Draw audio file",
         .description =
             "Draw a WAV or AIFF file as a PNG waveform with bar lines or a spectrogram, optionally from and "
             "to a time in seconds. Writes to out, by default the file's path with a png suffix.",
         .keywords = {"waveform", "spectrogram", "picture", "png"},
         .category = "Analyze",
         .schema = {{"required", {"path"}}, {"properties", imageSchema}},
         .run =
             [&controller, &registry](const Json& params) {
                 const std::filesystem::path file =
                     registry.checkedPath(param<std::string>(params, "path"), PathAccess::Read);
                 const std::filesystem::path out =
                     registry.checkedPath(optionalParam<std::string>(params, "out")
                                              .value_or(std::filesystem::path{file}.replace_extension("png").string()),
                                          PathAccess::Write);
                 return writeAudioImage(readAudio(file), params, out, controller.project().document().tempo);
             },
         .menus = {kFileMenu},
         .dialog =
             FileDialog{
                 .param = "path", .mode = FileDialogMode::Open, .nameFilter = "Audio files (*.wav *.aif *.aiff)"},
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
                 return withWarnings(analyzeSection(readAudio(file), params, document.tempo), controller);
             },
         .menus = {kFileMenu},
         .notifies = true});
    registry.addAction(
        {.id = kTrackAnalyzeAction,
         .title = "Analyze track",
         .description = "Render a track or bus alone and dry as its stem does, and measure integrated LUFS, sample "
                        "peak, true peak, active RMS (the median of 100 ms blocks above -60 dBFS), the share of the "
                        "energy in five bands with their edges, and the spectral centroid in Hz. from and to take "
                        "ticks or bar.beat.tick and default to the project start and end.",
         .keywords = {"loudness", "lufs", "true peak", "rms", "spectrum", "tone", "balance", "centroid"},
         .category = "Analyze",
         .schema = {{"required", {"track"}},
                    {"properties",
                     {{"track", {{"type", "string"}}}, {"from", Json::object()}, {"to", Json::object()}}}},
         .run =
             [&controller, &registry](const Json& params) {
                 return analyzeTrackRender(prepareTrackRender(controller, registry, params));
             },
         .notifies = true});
    registry.addAction(
        {.id = kTracksAnalyzeAction,
         .title = "Analyze all tracks",
         .description = "Run track.analyze on every audio track and bus, and list each one's name, id, integrated "
                        "LUFS, true peak, active RMS and five bands",
         .keywords = {"loudness", "lufs", "rms", "mix balance"},
         .category = "Analyze",
         .run =
             [&controller, &registry](const Json&) {
                 const model::Document document = controller.project().document();
                 const Json tracks =
                     analyzableTracks(registry, document) | std::views::transform([&](const model::TrackId& id) {
                         const Json measured =
                             analyzeTrackRender(prepareTrackRender(controller, registry, {{"track", id.value}}));
                         return Json{{"id", id.value},
                                     {"name", model::findTrack(document, id)->name},
                                     {"activeRmsDb", measured.at("activeRmsDb")},
                                     {"integratedLufs", measured.at("integratedLufs")},
                                     {"truePeakDbtp", measured.at("truePeakDbtp")},
                                     {"bands", measured.at("bands")}};
                     }) |
                     std::ranges::to<Json::array_t>();
                 return Json{{"message", std::format("{} tracks analyzed", tracks.size())}, {"tracks", tracks}};
             },
         .notifies = true});
}

}  // namespace emerick::app
