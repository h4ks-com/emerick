#include "emerick/app/voice_audition.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ranges>
#include <stdexcept>
#include <utility>

#include "emerick/app/action_params.hpp"
#include "emerick/app/audio_file.hpp"
#include "emerick/app/stem_export.hpp"
#include "emerick/core/midi.hpp"
#include "emerick/engine/note_analysis.hpp"

namespace emerick::app {

namespace {

constexpr int kMaxNotes = 32;
constexpr double kMaxSeconds = 10;
constexpr double kMinSeconds = 0.1;
constexpr int kMaxMidi = 127;
// A note holds for 3/4 of its time, so most of its release fades inside its own measurement.
constexpr core::Ticks kHeldParts = 3;
constexpr core::Ticks kNoteParts = 4;

constexpr std::array kSpread{36, 48, 60, 72, 84};
constexpr std::array kPhrase{48, 52, 55, 60};
constexpr double kPhraseSeconds = 0.4;

double rounded(double value, double step) { return std::round(value / step) * step; }

Json levelDb(double value) { return std::isfinite(value) ? Json(rounded(value, 0.1)) : Json(nullptr); }

const engine::ClapParam& voiceParam(const std::vector<engine::ClapParam>& params, const Json& given,
                                    const std::string& instrument) {
    if (const auto id = optionalParam<std::uint32_t>(given, "param")) {
        const auto found = std::ranges::find(params, *id, &engine::ClapParam::id);
        if (found == params.end() || found->choices.empty()) {
            throw AppError(instrument + " has no param " + std::to_string(*id) +
                           " with choices, run instrument.params for ids");
        }
        return *found;
    }
    const auto found =
        std::ranges::find_if(params, [](const engine::ClapParam& param) { return !param.choices.empty(); });
    if (found == params.end()) {
        throw AppError(instrument + " has no voices to audition, run instrument.params for its params");
    }
    return *found;
}

std::vector<int> notesParam(const Json& params) {
    std::vector<int> notes =
        optionalParam<std::vector<int>>(params, "notes").value_or(std::vector<int>(kSpread.begin(), kSpread.end()));
    if (notes.empty() || std::ssize(notes) > kMaxNotes ||
        std::ranges::any_of(notes, [](int note) { return note < 0 || note > kMaxMidi; })) {
        throw AppError(std::format("give 1 to {} notes from 0 to 127", kMaxNotes));
    }
    return notes;
}

void playOnTrack(ProjectController& controller, const std::string& trackId, std::vector<std::vector<float>> channels) {
    try {
        controller.engine().playPreview(trackId, std::move(channels));
    } catch (const std::runtime_error& error) {
        throw AppError(std::string{"cannot play the voice: "} + error.what());
    } catch (const std::invalid_argument& error) {
        throw AppError(std::string{"cannot play the voice: "} + error.what());
    }
}

Json noteJson(int note, const engine::NoteAnalysis& analysis) {
    return {{"note", note},
            {"peakDb", levelDb(analysis.peakDb)},
            {"activeRmsDb", levelDb(analysis.activeRmsDb)},
            {"centroidHz", std::round(analysis.centroidHz)},
            {"harmonicShare", rounded(analysis.harmonicShare, 0.001)}};
}

}  // namespace

Audition prepareAudition(const Registry& registry, ProjectController& controller, const Json& params) {
    const model::TrackId id = trackParam(controller.project(), params);
    const model::Track& track = *controller.project().track(id);
    if (!track.instrument) {
        throw AppError("track " + track.name + " has no instrument, load an instrument on this MIDI track first");
    }
    controller.instruments().sync(controller.project().document());
    const auto live = controller.instruments().find(id);
    if (!live) {
        throw AppError("the instrument of track " + track.name + " is not loaded, see the track's warning");
    }
    const std::vector<engine::ClapParam> choices = live->params();
    const engine::ClapParam& info = voiceParam(choices, params, live->name());
    Audition audition;
    audition.trackId = id.value;
    audition.slot = *track.instrument;
    audition.param = info.id;
    audition.slot.state = live->saveState().value_or(track.instrument->state);
    if (const auto voice = optionalParam<std::string>(params, "voice")) {
        const auto found = std::ranges::find(info.choices, *voice);
        if (found == info.choices.end()) {
            throw AppError(live->name() + " has no voice \"" + *voice + "\", run instrument.params for its choices");
        }
        audition.voice = *voice;
        audition.value = info.min + static_cast<double>(found - info.choices.begin());
    } else {
        audition.value = std::round(live->paramValue(info.id).value_or(info.defaultValue));
        audition.voice = live->paramText(info.id, audition.value);
    }
    audition.notes = notesParam(params);
    audition.seconds = optionalParam<double>(params, "seconds").value_or(1);
    if (audition.seconds < kMinSeconds || audition.seconds > kMaxSeconds || std::isnan(audition.seconds)) {
        throw AppError(std::format("give seconds per note from {} to {}", kMinSeconds, kMaxSeconds));
    }
    audition.velocity = optionalParam<int>(params, "velocity").value_or(audition.velocity);
    if (audition.velocity < 1 || audition.velocity > kMaxMidi) {
        throw AppError("give a velocity from 1 to 127");
    }
    audition.sampleRate = controller.sampleRate();
    if (const auto path = optionalParam<std::string>(params, "path")) {
        audition.path = registry.checkedPath(*path, PathAccess::Write);
    }
    audition.play = optionalParam<bool>(params, "play").value_or(false);
    return audition;
}

AuditionResult renderAudition(const Audition& audition, const std::stop_token& stop) {
    auto created = engine::ClapInstrument::create(audition.slot.bundle, audition.slot.pluginId);
    if (!created) {
        throw AppError("cannot audition " + audition.slot.name + ": " + created.error());
    }
    const std::shared_ptr<engine::ClapInstrument>& instrument = *created;
    if (!audition.slot.state.empty() && !instrument->loadState(audition.slot.state)) {
        throw AppError("cannot audition " + audition.slot.name + ": it did not load its settings");
    }
    instrument->setParam(audition.param, audition.value);

    const core::TempoMap tempo;
    const core::Ticks noteTicks =
        tempo.framesToTicks(std::llround(audition.seconds * audition.sampleRate), audition.sampleRate);
    const auto count = std::ssize(audition.notes);
    const auto noteFrame = [&](std::ptrdiff_t index) {
        return static_cast<std::size_t>(tempo.ticksToFrames(index * noteTicks, audition.sampleRate));
    };
    engine::MidiClip clip{.start = 0, .events = {}, .length = count * noteTicks};
    for (std::ptrdiff_t index = 0; index < count; ++index) {
        const auto pitch = static_cast<std::uint8_t>(audition.notes[static_cast<std::size_t>(index)]);
        const core::Ticks start = index * noteTicks;
        clip.events.push_back({start, {core::midi::kNoteOn, pitch, static_cast<std::uint8_t>(audition.velocity)}});
        clip.events.push_back({start + (noteTicks * kHeldParts / kNoteParts), {core::midi::kNoteOff, pitch, 0}});
    }
    AuditionResult result;
    result.channels = renderInstrument(instrument, std::move(clip), tempo,
                                       static_cast<engine::Frames>(noteFrame(count)), audition.sampleRate, stop);
    if (result.channels.empty()) {
        return result;
    }

    Json notes = Json::array();
    double lowestShare = 1;
    for (std::ptrdiff_t index = 0; index < count; ++index) {
        const int note = audition.notes[static_cast<std::size_t>(index)];
        const std::vector<std::vector<float>> played =
            result.channels | std::views::transform([&](const std::vector<float>& channel) {
                return std::vector<float>(channel.begin() + static_cast<std::ptrdiff_t>(noteFrame(index)),
                                          channel.begin() + static_cast<std::ptrdiff_t>(noteFrame(index + 1)));
            }) |
            std::ranges::to<std::vector>();
        const engine::NoteAnalysis analysis = engine::analyzeNote(played, audition.sampleRate, engine::noteHz(note));
        lowestShare = std::min(lowestShare, analysis.harmonicShare);
        notes.push_back(noteJson(note, analysis));
    }
    result.json = {
        {"track", audition.trackId},
        {"param", audition.param},
        {"voice", audition.voice},
        {"seconds", audition.seconds},
        {"velocity", audition.velocity},
        {"notes", std::move(notes)},
        {"message", std::format("{}: lowest harmonic share {:.2f} over {} notes", audition.voice, lowestShare, count)}};
    if (audition.path) {
        writeWav(*audition.path, result.channels, audition.sampleRate);
        result.json["path"] = audition.path->string();
    }
    return result;
}

void registerAuditionAction(Registry& registry, ProjectController& controller) {
    registry.addAction(
        {.id = kVoicesAuditionAction,
         .title = "Preview voice",
         .description =
             "Renders notes through a temporary copy of the track's instrument set to a voice, the exact text of a "
             "choice of its first param with choices or of param, by default its current one. The track and the "
             "project stay as they are. notes default to MIDI notes 36, 48, 60, 72 and 84, each held for 3/4 of "
             "seconds (default 1). Returns per note the peak, the active RMS, the spectral centroid in Hz and the "
             "harmonic share: the energy near multiples of the note's pitch over all energy, which a voice with "
             "decode artifacts or noise reads low. path writes the render to a WAV, and play plays it on the track.",
         .aliases = {"audition voice", "audition sound", "hear voice", "screen voices"},
         .keywords = {"preview", "sound", "harmonic", "artifacts", "noise", "centroid"},
         .category = "Tracks",
         .schema = {{"required", {"track"}},
                    {"properties",
                     {{"track", {{"type", "string"}}},
                      {"voice", {{"type", "string"}, {"description", "the exact text of one of the param's choices"}}},
                      {"param", {{"type", "integer"}, {"description", "a param id from instrument.params"}}},
                      {"notes", {{"type", "array"}, {"items", {{"type", "integer"}}}}},
                      {"seconds", {{"type", "number"}}},
                      {"velocity", {{"type", "integer"}}},
                      {"path", {{"type", "string"}}},
                      {"play", {{"type", "boolean"}}}}}},
         .run =
             [&registry, &controller](const Json& params) {
                 const Audition audition = prepareAudition(registry, controller, params);
                 AuditionResult result = renderAudition(audition);
                 if (audition.play) {
                     playOnTrack(controller, audition.trackId, std::move(result.channels));
                 }
                 return result.json;
             },
         .notifies = true});
}

VoicePreview::VoicePreview(const Registry& registry, ProjectController& controller, QObject* parent)
    : QObject(parent), registry_(registry), controller_(controller) {}

void VoicePreview::play(const QString& track, int param, const QString& voice) {
    if (!busyTrack_.isEmpty()) {
        return;
    }
    const Json params{{"track", track.toStdString()}, {"param", param},
                      {"voice", voice.toStdString()}, {"notes", kPhrase},
                      {"seconds", kPhraseSeconds},    {"play", true}};
    Audition audition;
    try {
        audition = prepareAudition(registry_, controller_, params);
    } catch (const AppError& error) {
        emit registry_.noticeRaised(qs(registry_.action(kVoicesAuditionAction)->title), qs(error.what()));
        return;
    }
    emit registry_.actionRan(kVoicesAuditionAction, params, ActionSource::Ui);
    busyTrack_ = track;
    emit busyTrackChanged();
    worker_ = std::jthread([this, track, audition = std::move(audition)](const std::stop_token& stop) {
        AuditionResult result;
        QString error;
        try {
            result = renderAudition(audition, stop);
        } catch (const AppError& failure) {
            error = qs(failure.what());
        }
        if (stop.stop_requested()) {
            return;
        }
        QMetaObject::invokeMethod(
            this, [this, track, result = std::move(result), error] { finish(track, result, error); },
            Qt::QueuedConnection);
    });
}

void VoicePreview::finish(const QString& track, const AuditionResult& result, const QString& error) {
    worker_.join();
    busyTrack_.clear();
    emit busyTrackChanged();
    QString failure = error;
    if (failure.isEmpty()) {
        try {
            playOnTrack(controller_, track.toStdString(), result.channels);
        } catch (const AppError& playing) {
            failure = qs(playing.what());
        }
    }
    if (!failure.isEmpty()) {
        emit registry_.noticeRaised(qs(registry_.action(kVoicesAuditionAction)->title), failure);
    }
}

}  // namespace emerick::app
