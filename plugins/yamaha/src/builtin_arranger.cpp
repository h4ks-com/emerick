#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/yamaha/profile.hpp"
#include "gm_sounds.hpp"
#include "registration.hpp"
#include "style_library.hpp"
#include "style_notes.hpp"
#include "voice_list.hpp"
#include "voices_params.hpp"

namespace emerick::yamaha {

namespace {

using app::AppError;
using app::Json;

// MegaVoices play strum and fret noises from C6 (MIDI 96) up, which a General MIDI sound plays as ringing notes.
constexpr int kMegaVoiceBank = 8;
constexpr int kHighestMegaVoiceNote = 95;

std::optional<model::TrackId> firstOfKind(const model::Document& document, const char* kind) {
    const auto found = std::ranges::find(document.tracks, std::string{kind}, &model::Track::kind);
    return found == document.tracks.end() ? std::nullopt : std::optional{found->id};
}

model::TrackId ensureTrack(const app::Registry& registry, app::ProjectController& controller, const char* kind) {
    if (const auto found = firstOfKind(controller.project().document(), kind)) {
        return *found;
    }
    const app::TrackType& type = *registry.trackType(kind);
    model::Track track;
    track.kind = kind;
    track.name = type.title;
    track.icon = type.icon;
    const model::TrackId id = track.id;
    addTrack(controller, std::move(track));
    return id;
}

std::optional<StyleSummary> loadedStyle(const model::Track& style) {
    const auto state = arrangerState(style);
    return state ? stateStyle(*state) : std::nullopt;
}

// The 1-based General MIDI program each part plays when the voice list names no category for its voice.
constexpr std::array<int, arranger::kParts> kPartDefaults{12, 12, 34, 28, 5, 49, 62, 82};

struct WaitingPart {
    std::size_t part = 0;
    arranger::PartVoice voice;
};

// What the built-in arranger keeps between actions. Only the main thread touches it.
struct BuiltinArranger {
    CachedVoiceList voiceList;
    // The part tracks left on Expansion Voices while it reads its store, by track id.
    std::map<std::string, WaitingPart> waitingParts;
};

// The 0-based General MIDI program that stands in for a voice no pack holds, or nullopt when General MIDI follows
// the style's own bank and program because the voice is a General MIDI one.
std::optional<int> standInProgram(const VoiceList& list, std::size_t part, const arranger::PartVoice& voice) {
    if (voice.bankMsb == 0 && voice.bankLsb == 0) {
        return std::nullopt;
    }
    const int program = voice.program + 1;
    const auto& categories = list.categories;
    if (const auto found = categories.find({voice.bankMsb, voice.bankLsb, program}); found != categories.end()) {
        const gm::Family family = found->second.family;
        return (program >= family.first && program <= family.last ? program : family.typical) - 1;
    }
    return kPartDefaults.at(part) - 1;
}

// The General MIDI kit the style's drum program names, or Standard when General MIDI holds no such kit.
std::size_t drumKitIndex(int program) {
    const gm::DrumKit* kit = gm::drumKitOf(program);
    return kit == nullptr ? 0 : static_cast<std::size_t>(kit - gm::kDrumKits.data());
}

// General MIDI plays the kit's notes as the voice list maps them, or unchanged when the list maps no note of the kit.
void writeDrumMap(const VoiceList& list, app::ProjectController& controller, const model::TrackId& id,
                  const arranger::PartVoice& voice) {
    const auto& maps = list.drumMaps;
    const auto map = maps.find({voice.bankMsb, voice.bankLsb, voice.program + 1});
    const std::optional<model::PluginSlot>& instrument = controller.project().track(id)->instrument;
    if (!instrument) {
        return;
    }
    Json state = Json::parse(instrument->state);
    if (map == maps.end()) {
        state.erase(gm::kDrumMapKey);
    } else {
        state[gm::kDrumMapKey] = map->second;
    }
    const std::string text = state.dump();
    app::editTrack(controller, id, "map drum notes",
                   [&](model::Track& track) { track.instrument.value().state.assign(text.begin(), text.end()); });
    controller.instruments().sync(controller.project().document());
}

bool playsVoice(const Json& params) {
    return std::ranges::any_of(params, [](const Json& param) {
        return param.at("id") == voices::kVoiceParam && param.at("value").get<double>() >= 1.0;
    });
}

void loadPartInstrument(BuiltinArranger& builtin, app::Registry& registry, app::ProjectController& controller,
                        const model::TrackId& id, std::size_t part, std::optional<arranger::PartVoice> voice);

// A part asks for the style's voice until someone loads another instrument or picks another voice on it.
bool asksForVoice(const model::Track& track, const arranger::PartVoice& voice) {
    if (!track.instrument || track.instrument->pluginId != kVoicesPluginId) {
        return false;
    }
    const Json state = Json::parse(track.instrument->state.begin(), track.instrument->state.end(), nullptr, false);
    if (!state.is_object() || !state.contains("voice") || !state.at("voice").is_object()) {
        return false;
    }
    const Json& asked = state.at("voice");
    return asked.value("msb", -1) == voice.bankMsb && asked.value("lsb", -1) == voice.bankLsb &&
           asked.value("program", -1) == voice.program;
}

// Once Expansion Voices read its store, a waiting part whose voice it lacks switches to its General MIDI stand-in.
void loadStandInsForMissingVoices(BuiltinArranger& builtin, app::Registry& registry,
                                  app::ProjectController& controller) {
    if (builtin.waitingParts.empty()) {
        return;
    }
    controller.project().group("play general midi for missing voices", [&] {
        for (const auto& [track, waiting] : std::map{builtin.waitingParts}) {
            const model::TrackId id{track};
            const model::Track* found = controller.project().track(id);
            const Json described = controller.instruments().describe(id);
            const bool voices = found != nullptr && asksForVoice(*found, waiting.voice);
            if (voices && voicesLoading(controller.instruments(), *found)) {
                continue;
            }
            builtin.waitingParts.erase(track);
            const bool plays = !described.is_null() && playsVoice(described.at("params"));
            if (voices && !plays) {
                loadPartInstrument(builtin, registry, controller, id, waiting.part, waiting.voice);
            }
        }
    });
}

// Loads Expansion Voices with the voice and returns its params, or nullopt when it failed to load.
std::optional<Json> chooseExpansionVoice(app::Registry& registry, app::ProjectController& controller,
                                         const model::TrackId& id, const arranger::PartVoice& voice) {
    registry.run("instrument.load", {{"track", id.value}, {"plugin", kVoicesPluginId}});
    const std::string state = Json{
        {"voice",
         {{"pack", ""}, {"name", ""}, {"msb", voice.bankMsb}, {"lsb", voice.bankLsb}, {"program", voice.program}}},
        {"followProgramChanges",
         true}}.dump();
    app::editTrack(controller, id, "pick voice",
                   [&](model::Track& track) { track.instrument->state.assign(state.begin(), state.end()); });
    // The project holds back change notices inside an undo group, so we sync the instruments ourselves.
    controller.instruments().sync(controller.project().document());
    const Json described = controller.instruments().describe(id);
    return described.is_null() ? std::nullopt : std::optional{described.at("params")};
}

void configureGmStandIn(BuiltinArranger& builtin, app::Registry& registry, app::ProjectController& controller,
                        const model::TrackId& id, std::size_t part, const arranger::PartVoice& voice) {
    controller.instruments().sync(controller.project().document());
    if (controller.instruments().find(id) == nullptr) {
        return;
    }
    registry.run("instrument.setParam", {{"track", id.value},
                                         {"param", gm::kHighestNoteParam},
                                         {"value", voice.bankMsb == kMegaVoiceBank ? kHighestMegaVoiceNote : 127}});
    if (gm::isDrumBank(voice.bankMsb)) {
        registry.run("instrument.setParam",
                     {{"track", id.value}, {"param", gm::kDrumKitParam}, {"value", drumKitIndex(voice.program)}});
        writeDrumMap(voiceList(builtin.voiceList, registry), controller, id, voice);
        return;
    }
    const std::optional<int> standIn = standInProgram(voiceList(builtin.voiceList, registry), part, voice);
    registry.run("instrument.setParam",
                 {{"track", id.value}, {"param", gm::kSoundParam}, {"value", standIn.value_or(voice.program)}});
    if (standIn) {
        registry.run("instrument.setParam", {{"track", id.value}, {"param", gm::kFollowParam}, {"value", 0}});
    }
}

// Expansion Voices plays the part when it holds the voice the style asks for; General MIDI plays every other part.
void loadPartInstrument(BuiltinArranger& builtin, app::Registry& registry, app::ProjectController& controller,
                        const model::TrackId& id, std::size_t part, std::optional<arranger::PartVoice> voice) {
    if (voice) {
        const std::optional<Json> params = chooseExpansionVoice(registry, controller, id, *voice);
        if (params && playsVoice(*params)) {
            return;
        }
        if (params && voicesLoading(controller.instruments(), *controller.project().track(id))) {
            builtin.waitingParts[id.value] = {part, *voice};
            return;
        }
    }
    registry.run("instrument.load", {{"track", id.value}, {"plugin", app::kGeneralMidiPluginId}});
    if (voice) {
        configureGmStandIn(builtin, registry, controller, id, part, *voice);
    }
}

using PartTracks = std::array<std::optional<model::TrackId>, arranger::kParts>;

PartTracks partTracks(const model::Document& document, const model::TrackId& style) {
    PartTracks tracks;
    for (std::size_t part = 0; part < arranger::kParts; ++part) {
        const int channel = genos2Parts().at(static_cast<std::size_t>(arranger::kFirstPartChannel) + part).channel;
        const auto found = std::ranges::find_if(document.tracks, [&](const model::Track& track) {
            return track.midiIn.sourceTrack == style && track.midiIn.channel == channel;
        });
        if (found != document.tracks.end()) {
            tracks.at(part) = found->id;
        }
    }
    return tracks;
}

// A part track follows the style while it plays General MIDI or Expansion Voices; any other instrument is the user's.
void followStyle(BuiltinArranger& builtin, app::Registry& registry, app::ProjectController& controller,
                 const model::TrackId& style, const StyleSummary& summary) {
    const PartTracks tracks = partTracks(controller.project().document(), style);
    for (std::size_t part = 0; part < arranger::kParts; ++part) {
        const std::optional<model::TrackId>& id = tracks.at(part);
        if (!id) {
            continue;
        }
        const std::optional<model::PluginSlot>& instrument = controller.project().track(*id)->instrument;
        if (!instrument) {
            continue;
        }
        const std::optional<arranger::PartVoice>& voice = summary.style.voices.at(part);
        const std::string plugin = instrument->pluginId;
        if (plugin == kVoicesPluginId || (plugin == app::kGeneralMidiPluginId && voice)) {
            loadPartInstrument(builtin, registry, controller, *id, part, voice);
        }
    }
}

}  // namespace

std::vector<engine::SessionError> partWarnings(const model::Track& style, const model::Document& document,
                                               const app::Instruments& instruments) {
    const auto summary = loadedStyle(style);
    if (!summary) {
        return {};
    }
    std::vector<engine::SessionError> warnings;
    const PartTracks tracks = partTracks(document, style.id);
    for (std::size_t part = 0; part < arranger::kParts; ++part) {
        const std::optional<arranger::PartVoice>& voice = summary->style.voices.at(part);
        const std::optional<model::TrackId>& id = tracks.at(part);
        if (!voice || !id) {
            continue;
        }
        const model::Track& track = *model::findTrack(document, *id);
        if (voicesLoading(instruments, track)) {
            warnings.push_back({.trackId = id->value,
                                .message = "Wait for Expansion Voices to read its voice packs before you play or "
                                           "render, since the part stays silent until then"});
        } else if (track.instrument && track.instrument->pluginId == app::kGeneralMidiPluginId) {
            warnings.push_back({.trackId = id->value,
                                .message = std::format("Install the pack with voice {}/{}/{} and choose Expansion "
                                                       "Voices to hear it in place of General MIDI",
                                                       static_cast<int>(voice->bankMsb),
                                                       static_cast<int>(voice->bankLsb), voice->program + 1)});
        }
    }
    return warnings;
}

namespace {

model::TrackId arrangerTrack(const app::ProjectController& controller, const app::Nav& nav, const Json& params) {
    const model::TrackId id = app::targetTrack(controller, nav, params);
    const model::Track& track = *controller.project().track(id);
    if (track.kind != kStyleKind || !arrangerState(track)) {
        throw AppError("track " + track.name + " plays no built-in arranger, run arranger.setup first");
    }
    return id;
}

Json setup(BuiltinArranger& builtin, app::Registry& registry, app::ProjectController& controller) {
    controller.project().group("set up built-in arranger", [&] {
        ensureTrack(registry, controller, kChordKind);
        const model::TrackId style = ensureTrack(registry, controller, kStyleKind);
        if (!arrangerState(*controller.project().track(style))) {
            registry.run("instrument.load", {{"track", style.value}, {"plugin", kArrangerPluginId}});
        }
        const auto summary = loadedStyle(*controller.project().track(style));
        const PartTracks tracks = partTracks(controller.project().document(), style);
        for (std::size_t part = 0; part < arranger::kParts; ++part) {
            const Part& info = genos2Parts().at(static_cast<std::size_t>(arranger::kFirstPartChannel) + part);
            if (tracks.at(part)) {
                continue;
            }
            const model::TrackId id{registry.run("track.addMidi", {{"name", std::string{info.name}}}).at("id")};
            registry.run("track.setMidiIn",
                         {{"track", id.value}, {"sourceTrack", style.value}, {"channel", info.channel}});
            app::editTrack(controller, id, "set up part", [&](model::Track& track) {
                track.icon = info.icon;
                track.armed = false;
                track.monitor = false;
            });
            loadPartInstrument(builtin, registry, controller, id, part,
                               summary ? summary->style.voices.at(part) : std::nullopt);
        }
    });
    return app::projectState(controller);
}

Json loadStyle(BuiltinArranger& builtin, app::Registry& registry, app::ProjectController& controller,
               const model::TrackId& id, const std::filesystem::path& path) {
    const auto bytes = readStyleBytes(path);
    const auto summary = bytes ? parseStyleSummary(path, *bytes) : std::nullopt;
    if (!summary) {
        throw AppError("cannot read " + path.string() + " as a style, pick a .sty, .prs or .sst file");
    }
    std::optional<ArrangerState> current = arrangerState(*controller.project().track(id));
    if (!current) {
        throw AppError("the track plays no built-in arranger, run arranger.setup first");
    }
    ArrangerState state = *current;
    state.style = path.string();
    state.hash = summary->hash;
    state.data = bytes->size() <= arranger::kEmbeddedStyleLimit ? *bytes : std::vector<std::uint8_t>{};
    controller.project().group("load style " + summary->name, [&] {
        app::editTrack(controller, id, "load style " + summary->name,
                       [&](model::Track& track) { track.instrument.value().state = stateBytes(state); });
        followStyle(builtin, registry, controller, id, *summary);
    });
    Json result = summaryJson(*summary);
    result["track"] = id.value;
    result["message"] = "Loaded " + summary->name;
    return result;
}

model::TrackId styleTrack(const app::ProjectController& controller, const app::Nav& nav, const Json& params) {
    if (params.contains("track")) {
        return arrangerTrack(controller, nav, params);
    }
    const auto styles = controller.project().document().tracks |
                        std::views::filter([](const model::Track& track) { return track.kind == kStyleKind; }) |
                        std::views::transform(&model::Track::id) | std::ranges::to<std::vector>();
    if (styles.empty()) {
        throw AppError("add a Style track with arranger.setup first");
    }
    if (styles.size() > 1) {
        throw AppError("pass track, the project has " + std::to_string(styles.size()) + " Style tracks");
    }
    return arrangerTrack(controller, nav, {{"track", styles.front().value}});
}

PackVoiceNames packVoiceNames(const app::ProjectController& controller, const model::TrackId& style,
                              const arranger::Style& loaded) {
    PackVoiceNames names;
    const PartTracks tracks = partTracks(controller.project().document(), style);
    for (std::size_t part = 0; part < arranger::kParts; ++part) {
        const std::optional<arranger::PartVoice>& voice = loaded.voices.at(part);
        const std::optional<model::TrackId>& id = tracks.at(part);
        if (!voice || !id || !asksForVoice(*controller.project().track(*id), *voice)) {
            continue;
        }
        const Json described = controller.instruments().describe(*id);
        if (!described.is_null() && playsVoice(described.at("params"))) {
            names.at(part) = described.at("params").at(0).at("text").get<std::string>();
        }
    }
    return names;
}

Json notes(BuiltinArranger& builtin, const app::Registry& registry, const app::ProjectController& controller,
           const app::Nav& nav, const Json& params) {
    const model::TrackId id = styleTrack(controller, nav, params);
    const auto summary = loadedStyle(*controller.project().track(id));
    if (!summary) {
        throw AppError("load a style with arranger.style.load first");
    }
    Json result = styleNotes(
        summary->style, voiceList(builtin.voiceList, registry), packVoiceNames(controller, id, summary->style),
        app::optionalParam<std::string>(params, "section"), app::optionalParam<std::string>(params, "part"));
    result["track"] = id.value;
    result["style"] = summary->name;
    return result;
}

}  // namespace

void registerBuiltinArranger(app::Registry& registry, app::ProjectController& controller, app::Nav& nav) {
    const auto builtin = std::make_shared<BuiltinArranger>();
    QObject::connect(
        &controller, &app::ProjectController::projectChanged, &controller,
        [builtin, &registry, &controller] { loadStandInsForMissingVoices(*builtin, registry, controller); });
    QObject::connect(&controller, &app::ProjectController::projectReplaced, &controller,
                     [builtin] { builtin->waitingParts.clear(); });
    registry.addAction(
        {.id = "arranger.setup",
         .title = "Set up built-in arranger",
         .description = "Adds Chords and Style tracks when the project lacks them, plays the Style track "
                        "through the Arranger, and adds a track per style part that plays it.",
         .aliases = {"built-in arranger", "play styles without a keyboard"},
         .keywords = {"style", "arranger", "yamaha"},
         .category = "Yamaha",
         .run = [builtin, &registry, &controller](const Json&) { return setup(*builtin, registry, controller); },
         .menus = {app::kTrackMenu, kYamahaMenu}});
    registry.addAction(
        {.id = "arranger.style.load",
         .title = "Choose style",
         .description = "Loads a style file into the Style track's Arranger and switches each part track between "
                        "Expansion Voices and General MIDI to match the style's voices, as one undo step.",
         .aliases = {"load style", "open style"},
         .category = "Yamaha",
         .schema = {{"required", {"path"}},
                    {"properties", {{"track", {{"type", "string"}}}, {"path", {{"type", "string"}}}}}},
         .run =
             [builtin, &registry, &controller, &nav](const Json& params) {
                 const model::TrackId id = arrangerTrack(controller, nav, params);
                 return loadStyle(*builtin, registry, controller, id,
                                  registry.checkedPath(app::param<std::string>(params, "path"), app::PathAccess::Read));
             },
         .menus = {kYamahaMenu},
         .dialog = app::FileDialog{.param = "path", .nameFilter = "Styles (*.sty *.prs *.sst)"}});
    registry.addAction({.id = "arranger.style.list",
                        .title = "List styles",
                        .description = "Lists the styles in the style folders with their tempo, meter and sections.",
                        .aliases = {"find style", "style browser"},
                        .category = "Yamaha",
                        .run = [&registry](const Json&) { return styleList(registry); },
                        .menus = {kYamahaMenu},
                        .notifies = true});
    registry.addAction(
        {.id = "arranger.style.notes",
         .title = "List style notes",
         .description = "Lists the notes each part of the loaded style plays per section, counted per note before "
                        "any chord transposes them, with each part's voice and each drum key's General MIDI sound "
                        "through the voice list.",
         .aliases = {"style parts", "what a part plays"},
         .category = "Yamaha",
         .schema =
             {{"properties",
               {{"track", {{"type", "string"}}}, {"section", {{"type", "string"}}}, {"part", {{"type", "string"}}}}}},
         .run = [builtin, &registry, &controller,
                 &nav](const Json& params) { return notes(*builtin, registry, controller, nav, params); },
         .menus = {kYamahaMenu},
         .notifies = true});
}

}  // namespace emerick::yamaha
