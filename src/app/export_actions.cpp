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
#include "emerick/app/analysis_render.hpp"
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

Json analysisJson(const engine::AudioAnalysis& analysis) {
    Json bands = Json::object();
    for (const auto& [band, db] : std::views::zip(engine::kFrequencyBands, analysis.bandDb)) {
        bands[std::string{band.name}] = levelDb(db);
    }
    Json result{{"integratedLufs", levelDb(analysis.levels.integratedLufs)},
                {"truePeakDbtp", levelDb(analysis.levels.truePeakDbtp)},
                {"samplePeakDbfs", levelDb(analysis.levels.samplePeakDbfs)},
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

Json analyzeTrack(ProjectController& controller, const Registry& registry, const Json& params) {
    return renderNow(prepareTrackRender(controller, registry, params)).json.front();
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
    result["message"] = loudnessMessage(analysis.levels.integratedLufs, analysis.levels.truePeakDbtp,
                                        std::format("{} clipped samples", analysis.clippedSamples));
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

AudioEncoding encodingParam(const Json& params, const std::filesystem::path& path) {
    return audioEncoding(path, optionalParam<std::string>(params, "format"), optionalParam<double>(params, "quality"));
}

}  // namespace

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

void registerExportActions(Registry& registry, ProjectController& controller, Nav& nav, ViewEngine& views) {
    registerExportSettings(registry);
    const Json tracksSchema{{"tracks", {{"type", "array"}}}};
    const Json formatSchema{{"type", "string"},
                            {"enum", kAudioFormats | std::views::keys | std::ranges::to<std::vector<std::string>>()}};
    const Json qualitySchema{{"type", "number"}, {"minimum", 0}, {"maximum", 1}};
    Json stemsSchema = tracksSchema;
    stemsSchema["format"] = formatSchema;
    stemsSchema["quality"] = qualitySchema;
    const Json textSchema{{"type", "string"}};
    stemsSchema["folder"] = textSchema;
    Json logicSchema = tracksSchema;
    logicSchema["folder"] = textSchema;
    Json midiSchema = tracksSchema;
    midiSchema["path"] = textSchema;
    registry.addAction(
        {.id = "export.stems",
         .title = "Export stems",
         .description = "Render each track and bus, or the listed tracks, to its own file in folder. format is wav "
                        "(the default), flac, ogg, opus or mp3, and quality from 0 to 1 (default 0.6) sets the "
                        "bitrate of ogg, opus and mp3.",
         .aliases = {"bounce tracks", "render stems"},
         .category = "Export",
         .schema = {{"required", {"folder"}}, {"properties", stemsSchema}},
         .run =
             [&controller, &registry](const Json& params) {
                 const std::filesystem::path folder =
                     registry.checkedPath(param<std::string>(params, "folder"), PathAccess::Write);
                 const AudioEncoding encoding =
                     audioEncoding({}, optionalParam<std::string>(params, "format").value_or("wav"),
                                   optionalParam<double>(params, "quality"));
                 const model::Document document = liveDocument(controller);
                 const engine::Session session = exportSession(controller, document, registry, params, false, true);
                 const auto files = exportStems(session, registry, playbackContext(controller, document), folder,
                                                controller.sampleRate(), false, encoding);
                 return withWarnings(Json{{"files", fileList(files)},
                                          {"end", positionState(exportEnd(session, document), document.tempo)}},
                                     controller);
             },
         .menus = {kFileMenu},
         .dialog = FileDialog{.param = "folder", .mode = FileDialogMode::Folder}});
    registry.addAction(
        {.id = "export.mix",
         .title = "Export mix",
         .description = "Render the mix to a stereo file at path, in format (wav, flac, ogg, opus or mp3) or else "
                        "the format of path's suffix; quality from 0 to 1 (default 0.6) sets the bitrate of ogg, "
                        "opus and mp3. With normalize, or when targetLufs or ceilingDbtp is "
                        "given, turn the mix to targetLufs integrated loudness and limit its true peak to "
                        "ceilingDbtp, and report the integratedLufs and truePeakDbtp the file ends at. Each one "
                        "defaults to its export setting.",
         .aliases = {"bounce mix", "render mix"},
         .keywords = {"normalize", "loudness", "lufs", "true peak", "limiter"},
         .category = "Export",
         .schema = {{"required", {"path"}},
                    {"properties",
                     {{"path", {{"type", "string"}}},
                      {"format", formatSchema},
                      {"quality", qualitySchema},
                      {"normalize", {{"type", "boolean"}}},
                      {"targetLufs", {{"type", "number"}, {"minimum", kMinTargetLufs}, {"maximum", 0}}},
                      {"ceilingDbtp", {{"type", "number"}, {"minimum", kMinCeilingDbtp}, {"maximum", 0}}}}}},
         .run =
             [&controller, &registry](const Json& params) {
                 const std::filesystem::path file =
                     registry.checkedPath(param<std::string>(params, "path"), PathAccess::Write);
                 const AudioEncoding encoding = encodingParam(params, file);
                 const model::Document document = liveDocument(controller);
                 const std::optional<MixLevels> levels =
                     exportMix(toSession(document, registry, controller.folder()), registry,
                               playbackContext(controller, document), file, controller.sampleRate(),
                               loudnessTarget(registry, params), encoding);
                 Json result{{"file", file.string()}};
                 if (levels) {
                     result["integratedLufs"] = levelDb(levels->integratedLufs);
                     result["truePeakDbtp"] = levelDb(levels->truePeakDbtp);
                 }
                 return withWarnings(result, controller);
             },
         .menus = {kFileMenu},
         .dialog = FileDialog{.param = "path",
                              .mode = FileDialogMode::Save,
                              .nameFilter = "Audio files (*.wav *.flac *.ogg *.opus *.mp3)",
                              .suffix = "wav"}});
    registerVideoActions(registry, controller, nav, views);
    registry.addAction({.id = "export.logic",
                        .title = "Export for Logic",
                        .aliases = {"logic pro", "send to logic"},
                        .keywords = {"stems", "midi file", "markers"},
                        .category = "Export",
                        .schema = {{"required", {"folder"}}, {"properties", logicSchema}},
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
         .schema = {{"required", {"path"}}, {"properties", midiSchema}},
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
    const Json rangeSchema{{"from", Json::object()}, {"to", Json::object()}, {"resolution", Json::object()}};
    Json trackSchema = rangeSchema;
    trackSchema["track"] = {{"type", "string"}};
    registry.addAction(
        {.id = kTrackAnalyzeAction,
         .title = "Analyze track",
         .description =
             "Render a track or bus alone and dry as its stem does, and measure integrated LUFS, sample peak, true "
             "peak, active RMS (the median of 100 ms blocks above -60 dBFS), the share of the energy in five bands "
             "with their edges, and the spectral centroid in Hz. from and to take ticks or bar.beat.tick and default "
             "to the project start and end. With resolution (bar, beat or a number of seconds) it adds windows: at, "
             "activeRmsDb, peakDb, centroidHz, the five band shares, tonalPeakHz (the strongest narrow peak above 1 "
             "kHz) with its tonality (0 for noise to 1 for a pure tone), crestDb and clipped samples, null in silence.",
         .keywords = {"loudness", "lufs", "true peak", "rms", "spectrum", "tone", "balance", "centroid", "windows"},
         .category = "Analyze",
         .schema = {{"required", {"track"}}, {"properties", trackSchema}},
         .run = [&controller, &registry](const Json& params) { return analyzeTrack(controller, registry, params); },
         .notifies = true});
    registry.addAction(
        {.id = kTracksAnalyzeAction,
         .title = "Analyze all tracks",
         .description = "Render the project once and measure every audio track and bus as track.analyze does, and "
                        "list each one's name, id, integrated LUFS, true peak, active RMS and five bands, and its "
                        "windows with resolution. from and to take ticks or bar.beat.tick and default to the project "
                        "start and end. Run mix.inspect to find issues by track and bar.",
         .keywords = {"loudness", "lufs", "rms", "mix balance"},
         .category = "Analyze",
         .schema = {{"properties", rangeSchema}},
         .run =
             [&controller, &registry](const Json& params) {
                 const model::Document& document = controller.project().document();
                 const std::vector<model::TrackId> ids = analyzableTracks(registry, document);
                 const Json measured =
                     ids.empty() ? Json::object() : renderNow(prepareTracksRender(controller, registry, params)).json;
                 const Json tracks = ids | std::views::transform([&](const model::TrackId& id) {
                                         const Json& levels = measured.at(id.value);
                                         Json track{{"id", id.value},
                                                    {"name", model::findTrack(document, id)->name},
                                                    {"activeRmsDb", levels.at("activeRmsDb")},
                                                    {"integratedLufs", levels.at("integratedLufs")},
                                                    {"truePeakDbtp", levels.at("truePeakDbtp")},
                                                    {"bands", levels.at("bands")}};
                                         if (levels.contains("windows")) {
                                             track["windows"] = levels.at("windows");
                                         }
                                         return track;
                                     }) |
                                     std::ranges::to<Json::array_t>();
                 return Json{{"message", std::format("{} tracks analyzed", tracks.size())}, {"tracks", tracks}};
             },
         .notifies = true});
    registry.addAction(
        {.id = kMixInspectAction,
         .title = "Inspect mix",
         .description =
             "Render the mix and every audio track and bus once, unmuted, and list the issues a listener would hear, "
             "most severe first: whine (a steady narrow tone), beep (a repeating high pitched hit, with its hz and "
             "hitsPerBeat), clipping, dc (a DC offset), imbalance (active RMS far from the median track), silent and "
             "harsh (a section much brighter than the track's average). Each issue names its severity, kind, track "
             "(an id, or master for the mix), from and to, a detail and the evidence that triggered it. from and to "
             "take ticks or bar.beat.tick; resolution is bar (the default), beat or a number of seconds. Run it after "
             "a render to find what to fix.",
         .keywords = {"issues", "problems", "whine", "beep", "clipping", "harsh", "balance", "check"},
         .category = "Analyze",
         .schema = {{"properties", rangeSchema}},
         .run = [&controller, &registry](
                    const Json& params) { return renderNow(prepareMixInspection(controller, registry, params)).json; },
         .menus = {kFileMenu},
         .notifies = true});
}

}  // namespace emerick::app
