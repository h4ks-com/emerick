#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/event_wait.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/app/stem_export.hpp"
#include "emerick/core/join.hpp"
#include "emerick/core/overloaded.hpp"
#include "emerick/model/controls.hpp"
#include "emerick/model/midi_notes.hpp"

namespace emerick::app {

namespace {

constexpr int kPitchClasses = 12;
constexpr std::array<std::string_view, kPitchClasses> kPitchNames{"C",  "C#", "D",  "D#", "E",  "F",
                                                                  "F#", "G",  "G#", "A",  "A#", "B"};
// We name pitches as the piano roll does, so 60 is C3.
constexpr int kLowestOctave = -2;
constexpr std::uint8_t kFirstChannelStatus = 0x80;
constexpr std::uint8_t kFirstSystemStatus = 0xF0;
constexpr std::uint8_t kChannelBits = 0x0F;
constexpr double kMeanPrecision = 10.0;
constexpr double kDefaultReadySeconds = 60;
constexpr double kMaxReadySeconds = 3600;

Json pitchState(int pitch) {
    const auto pitchClass = static_cast<std::size_t>(pitch % kPitchClasses);
    return {
        {"pitch", pitch},
        {"name", std::string{kPitchNames.at(pitchClass)} + std::to_string((pitch / kPitchClasses) + kLowestOctave)}};
}

int maxPolyphony(const std::vector<model::Note>& notes) {
    // We sort a note-off before a note-on at the same tick, so back to back notes never overlap.
    std::vector<std::pair<core::Ticks, int>> edges;
    for (const model::Note& note : notes) {
        edges.emplace_back(note.start, 1);
        edges.emplace_back(note.start + note.length, -1);
    }
    std::ranges::sort(edges);
    int held = 0;
    int most = 0;
    for (const auto& [at, step] : edges) {
        held += step;
        most = std::max(most, held);
    }
    return most;
}

std::set<int> channelsUsed(const model::MidiClip& midi) {
    std::set<int> channels;
    for (const model::Note& note : midi.notes) {
        channels.insert(note.channel);
    }
    for (const model::MidiEvent& event : midi.events) {
        if (!event.bytes.empty() && event.bytes[0] >= kFirstChannelStatus && event.bytes[0] < kFirstSystemStatus) {
            channels.insert((event.bytes[0] & kChannelBits) + 1);
        }
    }
    return channels;
}

Json controlRanges(const model::MidiClip& midi) {
    Json controls = Json::object();
    for (const model::Control control : model::controlsIn(midi)) {
        const std::vector<model::ControlPoint> points = model::controlPoints(midi, control);
        const auto [lowest, highest] = std::ranges::minmax(points | std::views::transform(&model::ControlPoint::value));
        controls[model::controlId(control)] = {{"min", lowest}, {"max", highest}, {"points", points.size()}};
    }
    return controls;
}

void addNoteStats(Json& result, const model::MidiClip& midi, const core::TempoMap& tempo) {
    const auto [lowest, highest] = std::ranges::minmax(midi.notes | std::views::transform(&model::Note::pitch));
    const auto [softest, loudest] = std::ranges::minmax(midi.notes | std::views::transform(&model::Note::velocity));
    const double mean =
        std::ranges::fold_left(midi.notes | std::views::transform(&model::Note::velocity), 0.0, std::plus{}) /
        static_cast<double>(midi.notes.size());
    std::map<int, int> perBar;
    for (const model::Note& note : midi.notes) {
        ++perBar[tempo.toBarBeatTick(midi.start + note.start).bar];
    }
    Json notesPerBar = Json::object();
    for (const auto& [bar, count] : perBar) {
        notesPerBar[std::to_string(bar)] = count;
    }
    result.update({{"lowest", pitchState(lowest)},
                   {"highest", pitchState(highest)},
                   {"velocity",
                    {{"min", softest}, {"mean", std::round(mean * kMeanPrecision) / kMeanPrecision}, {"max", loudest}}},
                   {"notesPerBar", std::move(notesPerBar)}});
}

Json clipAnalysis(const model::Track& track, const model::MidiClip& clip, const core::TempoMap& tempo) {
    const model::MidiClip midi = model::unrolled(clip);
    Json result{{"track", track.id.value},
                {"trackName", track.name},
                {"clip", clip.id.value},
                {"startBar", tempo.toBarBeatTick(clip.start).bar},
                {"lengthBars", static_cast<double>(model::clipSpan(clip)) / static_cast<double>(tempo.ticksPerBar())},
                {"noteCount", midi.notes.size()},
                {"maxPolyphony", maxPolyphony(midi.notes)},
                {"channels", channelsUsed(midi)},
                {"controls", controlRanges(midi)}};
    if (!midi.notes.empty()) {
        addNoteStats(result, midi, tempo);
    }
    return result;
}

Json analyzeMidi(const ProjectController& controller, const Json& params) {
    const model::Project& project = controller.project();
    const core::TempoMap& tempo = project.document().tempo;
    if (params.contains("clip")) {
        const model::ClipId id{param<std::string>(params, "clip")};
        const std::optional<model::ClipRef> found = project.findClip(id);
        if (!found) {
            throw AppError("no clip " + id.value + ", read the project state for clip ids");
        }
        const model::ClipRef& ref = *found;
        return std::visit(
            core::Overloaded{
                [&](const model::MidiClip& midi) { return Json{{"clips", {clipAnalysis(*ref.track, midi, tempo)}}}; },
                [](const model::AudioClip&) -> Json { throw AppError("the clip holds audio, pass a MIDI clip id"); }},
            *ref.clip);
    }
    const std::optional<model::TrackId> only =
        params.contains("track") ? std::optional{trackParam(project, params)} : std::nullopt;
    Json clips = Json::array();
    for (const model::Track& track : project.document().tracks) {
        if (only && track.id != *only) {
            continue;
        }
        for (const model::Clip& clip : track.clips) {
            std::visit(core::Overloaded{
                           [&](const model::MidiClip& midi) { clips.push_back(clipAnalysis(track, midi, tempo)); },
                           [](const model::AudioClip&) {}},
                       clip);
        }
    }
    return Json{{"clips", std::move(clips)}};
}

Json withMessage(Json clips) {
    std::size_t notes = 0;
    for (const Json& clip : clips) {
        notes += clip.value("noteCount", std::size_t{0});
    }
    return Json{{"message", std::format("{} MIDI clips, {} notes", clips.size(), notes)}, {"clips", std::move(clips)}};
}

std::size_t noteCount(const model::Track& track) {
    std::size_t count = 0;
    for (const model::Clip& clip : track.clips) {
        std::visit(core::Overloaded{[&](const model::MidiClip& midi) { count += midi.notes.size(); },
                                    [](const model::AudioClip&) {}},
                   clip);
    }
    return count;
}

Json trackSummary(const model::Track& track, const QVariantMap& warnings) {
    Json result{{"id", track.id.value},
                {"name", track.name},
                {"kind", track.kind},
                {"mute", track.mute},
                {"solo", track.solo},
                {"gainDb", track.gainDb},
                {"trimDb", track.trimDb},
                {"pan", track.pan},
                {"inserts", track.inserts | std::views::transform([](const model::Insert& insert) {
                                return Json{{"name", insertState(insert).at("title")}, {"bypass", insert.bypass}};
                            }) | std::ranges::to<Json::array_t>()},
                {"sends", track.sends | std::views::transform([](const model::Send& send) {
                              return Json{{"bus", send.bus.value}, {"db", send.levelDb}, {"enabled", send.enabled}};
                          }) | std::ranges::to<Json::array_t>()},
                {"clips", track.clips.size()},
                {"notes", noteCount(track)}};
    if (track.instrument) {
        result["instrument"] = track.instrument->name;
    }
    if (const QString warning = warnings.value(QString::fromStdString(track.id.value)).toString(); !warning.isEmpty()) {
        result["warning"] = warning.toStdString();
    }
    return result;
}

std::vector<model::TrackId> loadingTracks(const Registry& registry, const model::Document& document) {
    return registry.loadingTracks() |
           std::views::transform([&](const LoadingTracks& loading) { return loading(document); }) | std::views::join |
           std::ranges::to<std::vector>();
}

Json waitReady(ProjectController& controller, const Registry& registry, const Json& params) {
    const double seconds = optionalParam<double>(params, "timeout").value_or(kDefaultReadySeconds);
    if (seconds < 0 || seconds > kMaxReadySeconds) {
        throw AppError(std::format("give timeout in seconds from 0 to {}", kMaxReadySeconds));
    }
    controller.flush();
    const model::Document& document = controller.project().document();
    const auto ready = [&] { return loadingTracks(registry, document).empty(); };
    if (!processEventsUntil(ready, std::chrono::milliseconds{std::llround(seconds * 1000)})) {
        const auto names = core::join(
            loadingTracks(registry, document) |
                std::views::transform([&](const model::TrackId& id) { return model::findTrack(document, id)->name; }),
            ", ");
        throw AppError(std::format("{} still loading after {} s, wait longer or check the instrument", names, seconds));
    }
    return Json{{"ready", true}, {"message", "every instrument has loaded its sounds"}};
}

Json projectSummary(ProjectController& controller, const Registry& registry) {
    controller.flush();
    const model::Document& document = controller.project().document();
    const core::Ticks ticksPerBar = document.tempo.ticksPerBar();
    const QVariantMap warnings = controller.trackErrors();
    Json result{
        {"tempo", tempoState(document.tempo)},
        {"lengthBars",
         (exportEnd(toSession(document, registry, controller.folder()), document) + ticksPerBar - 1) / ticksPerBar},
        {"position", document.tempo.toBarBeatTick(controller.positionTicks()).format()},
        {"playing", controller.playing()},
        {"ready", loadingTracks(registry, document).empty()},
        {"tracks", document.tracks | std::views::transform([&](const model::Track& track) {
                       return trackSummary(track, warnings);
                   }) | std::ranges::to<Json::array_t>()},
        {"issues", controller.checkHealth()}};
    result["message"] = std::format("{} tracks, {} bars, {} issues", document.tracks.size(),
                                    result.at("lengthBars").get<core::Ticks>(), result.at("issues").size());
    if (!controller.sessionError().empty()) {
        result["sessionError"] = controller.sessionError();
    }
    return result;
}

}  // namespace

void registerSummaryActions(Registry& registry, ProjectController& controller) {
    registry.addAction(
        {.id = "midi.analyze",
         .title = "Analyze MIDI clips",
         .description = "Notes, range, velocity, notes per bar, polyphony, channels and controller "
                        "ranges of a clip, of a track's MIDI clips, or of every MIDI clip",
         .aliases = {"midi stats", "describe midi"},
         .category = "Clips",
         .schema = {{"properties", {{"track", {{"type", "string"}}}, {"clip", {{"type", "string"}}}}}},
         .run = [&controller](const Json& params) { return withMessage(analyzeMidi(controller, params).at("clips")); },
         .menus = {kTrackMenu},
         .notifies = true});
    registry.addAction({.id = "project.summary",
                        .title = "Summarize project",
                        .description = "Tempo, meter, length, position, each track's mix, devices and contents, and "
                                       "current warnings, in one compact result",
                        .aliases = {"project overview", "describe project"},
                        .category = "Project",
                        .run = [&controller, &registry](const Json&) { return projectSummary(controller, registry); },
                        .menus = {kFileMenu},
                        .notifies = true});
    registry.addAction(
        {.id = "project.waitReady",
         .title = "Wait for instruments to load",
         .description = "Returns once every instrument has loaded its sounds, such as Expansion Voices "
                        "reading its voice packs, and fails after timeout seconds (default 60)",
         .aliases = {"wait for voices", "wait until loaded"},
         .category = "Project",
         .schema = {{"properties", {{"timeout", {{"type", "number"}, {"minimum", 0}}}}}},
         .run = [&controller, &registry](const Json& params) { return waitReady(controller, registry, params); },
         .menus = {kFileMenu}});
}

}  // namespace emerick::app
