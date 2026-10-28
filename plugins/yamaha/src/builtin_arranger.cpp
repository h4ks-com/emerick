#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/yamaha/profile.hpp"
#include "gm_sounds.hpp"
#include "registration.hpp"
#include "style_library.hpp"

namespace emerick::yamaha {

namespace {

using app::AppError;
using app::Json;

constexpr std::size_t kFirstPart = 8;
constexpr std::uint32_t kVoiceParam = 0;
constexpr std::uint32_t kSoundParam = 0;
constexpr std::uint32_t kDrumKitParam = 1;
constexpr std::uint32_t kFollowParam = 2;
constexpr std::uint32_t kHighestNoteParam = 3;
// MegaVoices play strum and fret noises from C6 (MIDI 96) up, which a General MIDI sound plays as ringing notes.
constexpr int kMegaVoiceBank = 8;
constexpr int kHighestMegaVoiceNote = 95;

// Yamaha and Roland modules select a drum kit with these bank select MSBs, so General MIDI keeps its drum kit there.
constexpr bool isDrumBank(int msb) { return msb == 120 || msb == 126 || msb == 127; }

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
    return state && !state->style.empty() ? readStyle(state->style) : std::nullopt;
}

// A range of 1-based General MIDI programs and the one that stands for the whole family.
struct Family {
    int first = 1;
    int last = 1;
    int typical = 1;
};

struct Category {
    std::string_view name;
    Family family;
};

constexpr std::array<Category, 14> kCategories{{
    {"Piano", {1, 8, 1}},
    {"E.Piano", {5, 6, 5}},
    {"Percussion", {9, 16, 12}},
    {"Organ", {17, 21, 17}},
    {"Accordion", {22, 24, 22}},
    {"A.Guitar", {25, 26, 26}},
    {"E.Guitar", {27, 32, 28}},
    {"Bass", {33, 40, 34}},
    {"Strings", {41, 52, 49}},
    {"Choir", {53, 55, 53}},
    {"Brass", {57, 64, 62}},
    {"Woodwind", {65, 80, 66}},
    {"Synth", {81, 88, 81}},
    {"Pad", {89, 96, 90}},
}};

// The 1-based General MIDI program each part plays when the voice list names no category for its voice.
constexpr std::array<int, arranger::kParts> kPartDefaults{12, 12, 34, 28, 5, 49, 62, 82};

using VoiceKey = std::tuple<int, int, int>;
using VoiceFamilies = std::map<VoiceKey, Family>;

std::string_view trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    return first == std::string_view::npos ? std::string_view{}
                                           : text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

std::optional<int> numberIn(std::string_view field, int first, int last) {
    const std::string_view text = trimmed(field);
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    const bool whole = error == std::errc{} && end == text.data() + text.size();
    return whole && value >= first && value <= last ? std::optional{value} : std::nullopt;
}

// The General MIDI note each note of a kit plays, or -1 to drop it.
using DrumMap = std::array<int, 128>;

struct VoiceList {
    VoiceFamilies families;
    std::map<VoiceKey, DrumMap> drumMaps;
};

void readDrumNote(VoiceList& list, std::span<const std::string_view, 5> fields) {
    const auto msb = numberIn(fields[0], 0, 127);
    const auto lsb = numberIn(fields[1], 0, 127);
    const auto program = numberIn(fields[2], 1, 128);
    const auto note = numberIn(fields[3], 0, 127);
    const auto played = numberIn(fields[4], -1, 127);
    if (msb && lsb && program && note && played) {
        auto [map, added] = list.drumMaps.try_emplace({*msb, *lsb, *program});
        if (added) {
            map->second.fill(-1);
        }
        map->second.at(static_cast<std::size_t>(*note)) = *played;
    }
}

VoiceList readVoiceList(const std::filesystem::path& path) {
    VoiceList list;
    std::ifstream file(path);
    for (std::string line; std::getline(file, line);) {
        const auto fields = line | std::views::split(',') |
                            std::views::transform([](auto field) { return std::string_view(field); }) |
                            std::ranges::to<std::vector>();
        if (fields.size() == 5) {
            readDrumNote(list, std::span<const std::string_view, 5>(fields));
        }
        if (fields.size() != 4) {
            continue;
        }
        const auto msb = numberIn(fields[0], 0, 127);
        const auto lsb = numberIn(fields[1], 0, 127);
        const auto program = numberIn(fields[2], 1, 128);
        const auto* const category = std::ranges::find(kCategories, trimmed(fields[3]), &Category::name);
        if (msb && lsb && program && category != kCategories.end()) {
            list.families[{*msb, *lsb, *program}] = category->family;
        }
    }
    return list;
}

const VoiceList& voiceList(const app::Registry& registry) {
    struct Cached {
        std::filesystem::path path;
        std::filesystem::file_time_type modified;
        VoiceList list;
    };
    static Cached cached;
    const std::filesystem::path path = registry.settingValue(kVoiceListSetting).get<std::string>();
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) {
        cached = {};
    } else if (path != cached.path || modified != cached.modified) {
        cached = {.path = path, .modified = modified, .list = readVoiceList(path)};
    }
    return cached.list;
}

// The 0-based General MIDI program that stands in for a voice no pack holds, or nullopt when General MIDI follows
// the style's own bank and program because the voice is a General MIDI one.
std::optional<int> standInProgram(const app::Registry& registry, std::size_t part, const arranger::PartVoice& voice) {
    if (voice.bankMsb == 0 && voice.bankLsb == 0) {
        return std::nullopt;
    }
    const int program = voice.program + 1;
    const VoiceFamilies& families = voiceList(registry).families;
    if (const auto found = families.find({voice.bankMsb, voice.bankLsb, program}); found != families.end()) {
        const Family family = found->second;
        return (program >= family.first && program <= family.last ? program : family.typical) - 1;
    }
    return kPartDefaults.at(part) - 1;
}

// The General MIDI kit the style's drum program names, or Standard when General MIDI holds no such kit.
std::size_t drumKitIndex(int program) {
    const auto* const kit = std::ranges::find(gm::kDrumKits, program, &gm::DrumKit::program);
    return kit == gm::kDrumKits.end() ? 0 : static_cast<std::size_t>(kit - gm::kDrumKits.begin());
}

// General MIDI plays the kit's notes as the voice list maps them, or unchanged when the list maps no note of the kit.
void writeDrumMap(const app::Registry& registry, app::ProjectController& controller, const model::TrackId& id,
                  const arranger::PartVoice& voice) {
    const auto& maps = voiceList(registry).drumMaps;
    const auto map = maps.find({voice.bankMsb, voice.bankLsb, voice.program + 1});
    const std::optional<model::PluginSlot>& instrument = controller.project().track(id)->instrument;
    if (!instrument) {
        return;
    }
    Json state = Json::parse(instrument->state);
    if (map == maps.end()) {
        state.erase("drumMap");
    } else {
        state["drumMap"] = map->second;
    }
    const std::string text = state.dump();
    app::editTrack(controller, id, "map drum notes",
                   [&](model::Track& track) { track.instrument.value().state.assign(text.begin(), text.end()); });
    controller.instruments().sync(controller.project().document());
}

// Expansion Voices names no voice with this label while it reads its store.
constexpr std::string_view kVoicesLoadingLabel = "Loading voices";

bool voicesLoading(const Json& params) {
    return std::ranges::any_of(params, [](const Json& param) {
        return param.at("id") == kVoiceParam && param.at("text") == kVoicesLoadingLabel;
    });
}

struct WaitingPart {
    std::size_t part = 0;
    arranger::PartVoice voice;
};

// The part tracks left on Expansion Voices while it reads its store, by track id. Only the main thread touches them.
std::map<std::string, WaitingPart>& waitingParts() {
    static std::map<std::string, WaitingPart> parts;
    return parts;
}

void loadPartInstrument(app::Registry& registry, app::ProjectController& controller, const model::TrackId& id,
                        std::size_t part, std::optional<arranger::PartVoice> voice);

// Once Expansion Voices read its store, a waiting part whose voice it lacks switches to its General MIDI stand-in.
void loadStandInsForMissingVoices(app::Registry& registry, app::ProjectController& controller) {
    if (waitingParts().empty()) {
        return;
    }
    controller.project().group("play general midi for missing voices", [&] {
        for (const auto& [track, waiting] : std::map{waitingParts()}) {
            const model::TrackId id{track};
            const model::Track* found = controller.project().track(id);
            const Json described = controller.instruments().describe(id);
            const bool voices = found != nullptr && found->instrument && found->instrument->pluginId == kVoicesPluginId;
            if (voices && !described.is_null() && voicesLoading(described.at("params"))) {
                continue;
            }
            waitingParts().erase(track);
            const bool plays =
                !described.is_null() && std::ranges::any_of(described.at("params"), [](const Json& param) {
                    return param.at("id") == kVoiceParam && param.at("value").get<double>() >= 1.0;
                });
            if (voices && !plays) {
                loadPartInstrument(registry, controller, id, waiting.part, waiting.voice);
            }
        }
    });
}

// Expansion Voices plays the part when it holds the voice the style asks for; General MIDI plays every other part.
void loadPartInstrument(app::Registry& registry, app::ProjectController& controller, const model::TrackId& id,
                        std::size_t part, std::optional<arranger::PartVoice> voice) {
    if (voice) {
        registry.run("instrument.load", {{"track", id.value}, {"plugin", kVoicesPluginId}});
        const std::string state = Json{
            {"voice",
             {{"pack", ""},
              {"name", ""},
              {"msb", voice->bankMsb},
              {"lsb", voice->bankLsb},
              {"program", voice->program}}},
            {"followProgramChanges",
             true}}.dump();
        app::editTrack(controller, id, "pick voice",
                       [&](model::Track& track) { track.instrument->state.assign(state.begin(), state.end()); });
        // The project holds back change notices inside an undo group, so we sync the instruments ourselves.
        controller.instruments().sync(controller.project().document());
        const Json described = controller.instruments().describe(id);
        const auto found = described.is_null() ? std::nullopt : std::optional{described.at("params")};
        const bool plays = found && std::ranges::any_of(*found, [](const Json& param) {
                               return param.at("id") == kVoiceParam && param.at("value").get<double>() >= 1.0;
                           });
        if (plays) {
            return;
        }
        if (found && voicesLoading(*found)) {
            waitingParts()[id.value] = {part, *voice};
            return;
        }
    }
    registry.run("instrument.load", {{"track", id.value}, {"plugin", app::kGeneralMidiPluginId}});
    if (!voice) {
        return;
    }
    controller.instruments().sync(controller.project().document());
    if (controller.instruments().find(id) == nullptr) {
        return;
    }
    registry.run("instrument.setParam", {{"track", id.value},
                                         {"param", kHighestNoteParam},
                                         {"value", voice->bankMsb == kMegaVoiceBank ? kHighestMegaVoiceNote : 127}});
    if (isDrumBank(voice->bankMsb)) {
        registry.run("instrument.setParam",
                     {{"track", id.value}, {"param", kDrumKitParam}, {"value", drumKitIndex(voice->program)}});
        writeDrumMap(registry, controller, id, *voice);
        return;
    }
    const std::optional<int> standIn = standInProgram(registry, part, *voice);
    registry.run("instrument.setParam",
                 {{"track", id.value}, {"param", kSoundParam}, {"value", standIn.value_or(voice->program)}});
    if (standIn) {
        registry.run("instrument.setParam", {{"track", id.value}, {"param", kFollowParam}, {"value", 0}});
    }
}

std::optional<model::TrackId> partTrack(const model::Document& document, const model::TrackId& style, int channel) {
    const auto found = std::ranges::find_if(document.tracks, [&](const model::Track& track) {
        return track.midiIn.sourceTrack == style && track.midiIn.channel == channel;
    });
    return found == document.tracks.end() ? std::nullopt : std::optional{found->id};
}

// A part track follows the style while it plays General MIDI or Expansion Voices; any other instrument is the user's.
void followStyle(app::Registry& registry, app::ProjectController& controller, const model::TrackId& style,
                 const StyleSummary& summary) {
    for (std::size_t part = 0; part < arranger::kParts; ++part) {
        const auto id = partTrack(controller.project().document(), style, genos2Parts().at(kFirstPart + part).channel);
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
            loadPartInstrument(registry, controller, *id, part, voice);
        }
    }
}

}  // namespace

std::vector<engine::SessionError> missingVoices(const model::Track& style, const model::Document& document) {
    const auto summary = loadedStyle(style);
    if (!summary) {
        return {};
    }
    std::vector<engine::SessionError> warnings;
    for (std::size_t part = 0; part < arranger::kParts; ++part) {
        const std::optional<arranger::PartVoice>& voice = summary->style.voices.at(part);
        const auto id = partTrack(document, style.id, genos2Parts().at(kFirstPart + part).channel);
        if (!voice || !id) {
            continue;
        }
        const model::Track& track = *std::ranges::find(document.tracks, *id, &model::Track::id);
        if (track.instrument && track.instrument->pluginId == app::kGeneralMidiPluginId) {
            warnings.push_back({.trackId = id->value,
                                .message = std::format("General MIDI stands in for voice {}/{}/{}. Install its pack "
                                                       "and choose Expansion Voices to hear it",
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

Json setup(app::Registry& registry, app::ProjectController& controller) {
    controller.project().group("set up built-in arranger", [&] {
        ensureTrack(registry, controller, kChordKind);
        const model::TrackId style = ensureTrack(registry, controller, kStyleKind);
        if (!arrangerState(*controller.project().track(style))) {
            registry.run("instrument.load", {{"track", style.value}, {"plugin", kArrangerPluginId}});
        }
        const auto summary = loadedStyle(*controller.project().track(style));
        for (std::size_t part = 0; part < arranger::kParts; ++part) {
            const Part& info = genos2Parts().at(kFirstPart + part);
            if (partTrack(controller.project().document(), style, info.channel)) {
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
            loadPartInstrument(registry, controller, id, part, summary ? summary->style.voices.at(part) : std::nullopt);
        }
    });
    return app::projectState(controller);
}

Json loadStyle(app::Registry& registry, app::ProjectController& controller, const model::TrackId& id,
               const std::filesystem::path& path) {
    const auto summary = readStyle(path);
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
    controller.project().group("load style " + summary->name, [&] {
        app::editTrack(controller, id, "load style " + summary->name,
                       [&](model::Track& track) { track.instrument.value().state = stateBytes(state); });
        followStyle(registry, controller, id, *summary);
    });
    Json result = summaryJson(*summary);
    result["track"] = id.value;
    result["message"] = "Loaded " + summary->name;
    return result;
}

}  // namespace

void registerBuiltinArranger(app::Registry& registry, app::ProjectController& controller, app::Nav& nav) {
    QObject::connect(&controller, &app::ProjectController::projectChanged, &controller,
                     [&registry, &controller] { loadStandInsForMissingVoices(registry, controller); });
    registry.addAction({.id = "arranger.setup",
                        .title = "Set up built-in arranger",
                        .description =
                            "Adds Chords and Style tracks when the project lacks them, plays the Style track "
                            "through the Arranger, and adds a track per style part that plays it.",
                        .aliases = {"built-in arranger", "play styles without a keyboard"},
                        .keywords = {"style", "arranger", "yamaha"},
                        .category = "Yamaha",
                        .run = [&registry, &controller](const Json&) { return setup(registry, controller); },
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
             [&registry, &controller, &nav](const Json& params) {
                 const model::TrackId id = arrangerTrack(controller, nav, params);
                 return loadStyle(registry, controller, id,
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
}

}  // namespace emerick::yamaha
