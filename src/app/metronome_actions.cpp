#include <algorithm>
#include <array>
#include <charconv>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/core_actions.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/session_mapping.hpp"

namespace emerick::app {

namespace {

constexpr const char* kPrefix = "metronome.";
constexpr const char* kEnabled = "metronome.enabled";
constexpr const char* kDuringPlayback = "metronome.duringPlayback";
constexpr const char* kDuringRecording = "metronome.duringRecording";
constexpr const char* kCountIn = "metronome.countIn";
constexpr const char* kMode = "metronome.mode";
constexpr const char* kPattern = "metronome.pattern";
constexpr const char* kVolume = "metronome.volume";
constexpr const char* kAccent = "metronome.accent";
constexpr const char* kAccentBeats = "metronome.accentBeats";
constexpr const char* kOutputDevice = "metronome.outputDevice";
constexpr const char* kOutputChannels = "metronome.outputChannels";
constexpr const char* kAccentSample = "metronome.accentSample";
constexpr const char* kBeatSample = "metronome.beatSample";
constexpr const char* kMidiPort = "metronome.midiPort";
constexpr const char* kMidiChannel = "metronome.midiChannel";
constexpr double kMinVolumeDb = -60.0;
constexpr double kMaxVolumeDb = 12.0;
constexpr double kMaxAccentDb = 18.0;
constexpr int kDefaultVolumeDb = -6;
constexpr int kDefaultAccentDb = 6;
constexpr int kMaxNote = 127;
constexpr int kDrumChannel = 10;

// A choice's text is both its stored value and what the UI shows.
template <class Enum>
struct Named {
    Enum value;
    const char* id;
};

constexpr std::array kModes{
    Named<engine::MetronomeMode>{engine::MetronomeMode::click, "Click"},
    Named<engine::MetronomeMode>{engine::MetronomeMode::sample, "Sample"},
    Named<engine::MetronomeMode>{engine::MetronomeMode::groove, "Drum groove"},
    Named<engine::MetronomeMode>{engine::MetronomeMode::midiClick, "MIDI click"},
    Named<engine::MetronomeMode>{engine::MetronomeMode::midiGroove, "MIDI groove"},
};

constexpr std::array kPatterns{
    Named<engine::GroovePattern>{engine::GroovePattern::rock, "Basic rock"},
    Named<engine::GroovePattern>{engine::GroovePattern::fourOnTheFloor, "Four on the floor"},
    Named<engine::GroovePattern>{engine::GroovePattern::halfTime, "Half-time"},
    Named<engine::GroovePattern>{engine::GroovePattern::shuffle, "Shuffle"},
    Named<engine::GroovePattern>{engine::GroovePattern::bossa, "Bossa"},
};

struct NoteSetting {
    engine::DrumVoice voice;
    const char* id;
    const char* title;
    const char* hint;
};

constexpr std::array kNoteSettings{
    NoteSetting{engine::DrumVoice::accent, "metronome.note.accent", "Accent note",
                "76 is the General MIDI high wood block"},
    NoteSetting{engine::DrumVoice::beat, "metronome.note.beat", "Beat note", "77 is the General MIDI low wood block"},
    NoteSetting{engine::DrumVoice::kick, "metronome.note.kick", "Kick note", "36 in General MIDI"},
    NoteSetting{engine::DrumVoice::snare, "metronome.note.snare", "Snare note", "38 in General MIDI"},
    NoteSetting{engine::DrumVoice::hat, "metronome.note.hat", "Hat note", "42 is the General MIDI closed hat"},
};

template <class Enum, std::size_t Count>
Json ids(const std::array<Named<Enum>, Count>& table) {
    return table | std::views::transform(&Named<Enum>::id) | std::ranges::to<Json::array_t>();
}

template <class Enum, std::size_t Count>
Enum lookup(const std::array<Named<Enum>, Count>& table, const std::string& id) {
    const auto found = std::ranges::find(table, id, &Named<Enum>::id);
    return found == table.end() ? table.front().value : found->value;
}

template <class Enum, std::size_t Count>
std::vector<MenuChoice> choices(const std::array<Named<Enum>, Count>& table, const char* param) {
    return table | std::views::transform([param](const Named<Enum>& entry) {
               return MenuChoice{.id = entry.id, .title = entry.id, .params = {{param, entry.id}}};
           }) |
           std::ranges::to<std::vector>();
}

std::string countInTitle(int bars) {
    if (bars == 0) {
        return "Off";
    }
    return std::to_string(bars) + (bars == 1 ? " bar" : " bars");
}

std::vector<int> numbers(std::string_view text) {
    std::vector<int> result;
    for (const auto word : std::views::split(text, ' ')) {
        int number = 0;
        const std::string_view digits{word.begin(), word.end()};
        if (std::from_chars(digits.data(), digits.data() + digits.size(), number).ec == std::errc{}) {
            result.push_back(number);
        }
    }
    return result;
}

engine::Metronome metronomeFromSettings(const Registry& registry) {
    const auto value = [&](const char* id) { return registry.settingValue(id); };
    engine::Metronome metronome{
        .enabled = value(kEnabled).get<bool>(),
        .duringPlayback = value(kDuringPlayback).get<bool>(),
        .duringRecording = value(kDuringRecording).get<bool>(),
        .countInBars = value(kCountIn).get<int>(),
        .mode = lookup(kModes, value(kMode).get<std::string>()),
        .pattern = lookup(kPatterns, value(kPattern).get<std::string>()),
        .gain = gainFromDb(value(kVolume).get<double>()),
        .accentGain = gainFromDb(value(kAccent).get<double>()),
        .accentBeats = numbers(value(kAccentBeats).get<std::string>()),
        .audioOut = {value(kOutputDevice).get<std::string>(), numbers(value(kOutputChannels).get<std::string>())},
        .accentSample = value(kAccentSample).get<std::string>(),
        .beatSample = value(kBeatSample).get<std::string>(),
        .midiOut = {value(kMidiPort).get<std::string>(), value(kMidiChannel).get<int>()},
    };
    for (const NoteSetting& note : kNoteSettings) {
        metronome.notes[static_cast<std::size_t>(note.voice)] = value(note.id).get<std::uint8_t>();
    }
    return metronome;
}

Json metronomeState(const Registry& registry, const ProjectController& controller) {
    Json state = Json::object();
    for (const Setting& setting : registry.allSettings()) {
        if (setting.id.starts_with(kPrefix)) {
            state[setting.id.substr(std::string_view{kPrefix}.size())] = registry.settingValue(setting.id);
        }
    }
    Json errors = Json::array();
    for (const auto* list : {&controller.engine().sessionErrors(), &controller.routeErrors()}) {
        for (const engine::SessionError& error : *list) {
            if (error.trackId == engine::kMetronomeId) {
                errors.push_back(error.message);
            }
        }
    }
    state["errors"] = errors;
    return state;
}

Json audioOutputs(const ProjectController& controller) {
    Json outputs = Json::array({{{"id", ""}, {"name", "Main output"}}});
    for (const engine::AudioDeviceInfo& device : controller.engine().audioDevices()) {
        if (device.outputChannels > 0) {
            outputs.push_back({{"id", device.id}, {"name", device.name}});
        }
    }
    return outputs;
}

Json midiOutputs(const ProjectController& controller) {
    return controller.engine().midiPorts() |
           std::views::filter([](const engine::MidiPortInfo& port) { return !port.isInput; }) |
           std::views::transform(
               [](const engine::MidiPortInfo& port) { return Json{{"id", port.id}, {"name", port.name}}; }) |
           std::ranges::to<Json::array_t>();
}

std::function<Json(const Json&, const Json&)> samplePath(const Registry& registry) {
    return [&registry](const Json&, const Json& change) {
        if (!change.is_string() || change.get<std::string>().empty()) {
            return change;
        }
        return Json(registry.checkedPath(change.get<std::string>(), PathAccess::Read).string());
    };
}

void addSetting(Registry& registry, Setting setting) {
    setting.category = "Settings / Metronome";
    setting.location = "settings.metronome";
    registry.addSetting(std::move(setting));
}

void registerSettings(Registry& registry, const ProjectController& controller, Nav& nav) {
    addSettingsSection(registry, nav, {.key = "metronome", .title = "Metronome"});
    addSetting(registry, {.id = kEnabled,
                          .title = "Metronome",
                          .aliases = {"click", "click track"},
                          .type = SettingType::Boolean,
                          .defaultValue = false,
                          .hint = "Play a click on the beat. Press K to turn it on or off"});
    addSetting(registry, {.id = kDuringPlayback,
                          .title = "Click during playback",
                          .type = SettingType::Boolean,
                          .defaultValue = false,
                          .hint = "Click while the project plays without recording"});
    addSetting(registry, {.id = kDuringRecording,
                          .title = "Click during recording",
                          .type = SettingType::Boolean,
                          .defaultValue = true,
                          .hint = "Click while recording"});
    addSetting(registry, {.id = kCountIn,
                          .title = "Count-in bars",
                          .aliases = {"pre-roll", "count in"},
                          .type = SettingType::Integer,
                          .defaultValue = 0,
                          .minimum = 0,
                          .maximum = engine::kMaxCountInBars,
                          .hint = "Bars of click before a recording from stop starts, even with the metronome off"});
    addSetting(registry, {.id = kMode,
                          .title = "Sound",
                          .aliases = {"metronome mode", "click sound"},
                          .type = SettingType::Choice,
                          .defaultValue = kModes.front().id,
                          .choices = ids(kModes),
                          .hint = "A click, your samples, a drum groove, or MIDI notes for an external instrument"});
    addSetting(registry, {.id = kPattern,
                          .title = "Groove pattern",
                          .aliases = {"drum beat", "drum pattern"},
                          .type = SettingType::Choice,
                          .defaultValue = kPatterns.front().id,
                          .choices = ids(kPatterns),
                          .hint = "Drum pattern the groove sounds play"});
    addSetting(registry, {.id = kVolume,
                          .title = "Volume",
                          .aliases = {"click volume", "metronome level"},
                          .type = SettingType::Number,
                          .defaultValue = kDefaultVolumeDb,
                          .minimum = kMinVolumeDb,
                          .maximum = kMaxVolumeDb,
                          .hint = "Level of the audio click in dB"});
    addSetting(registry, {.id = kAccent,
                          .title = "Accent level",
                          .type = SettingType::Number,
                          .defaultValue = kDefaultAccentDb,
                          .minimum = 0,
                          .maximum = kMaxAccentDb,
                          .hint = "dB the accented beats play above the others"});
    addSetting(registry, {.id = kAccentBeats,
                          .title = "Accented beats",
                          .aliases = {"downbeat accent"},
                          .type = SettingType::String,
                          .defaultValue = "1",
                          .hint = "Beats of the bar to accent, such as 1 3"});
    addSetting(registry, {.id = kOutputDevice,
                          .title = "Output device",
                          .aliases = {"click output", "headphone output"},
                          .type = SettingType::String,
                          .defaultValue = "",
                          .hint = "Device for the click, such as a headphone output",
                          .suggestions = [&controller] { return audioOutputs(controller); }});
    addSetting(registry, {.id = kOutputChannels,
                          .title = "Output channels",
                          .type = SettingType::String,
                          .defaultValue = "1 2",
                          .hint = "Channels of the output device, such as 3 4"});
    addSetting(registry, {.id = kAccentSample,
                          .title = "Accent sample",
                          .type = SettingType::String,
                          .defaultValue = "",
                          .hint = "Path of a short WAV for accented beats in the Sample sound",
                          .merge = samplePath(registry)});
    addSetting(registry, {.id = kBeatSample,
                          .title = "Beat sample",
                          .type = SettingType::String,
                          .defaultValue = "",
                          .hint = "Path of a short WAV for the other beats in the Sample sound",
                          .merge = samplePath(registry)});
    addSetting(registry, {.id = kMidiPort,
                          .title = "MIDI port",
                          .aliases = {"metronome midi output"},
                          .type = SettingType::String,
                          .defaultValue = "",
                          .hint = "Port the MIDI sounds send notes to",
                          .suggestions = [&controller] { return midiOutputs(controller); }});
    addSetting(registry, {.id = kMidiChannel,
                          .title = "MIDI channel",
                          .type = SettingType::Integer,
                          .defaultValue = kDrumChannel,
                          .minimum = 1,
                          .maximum = kMidiChannels,
                          .hint = "Channel 10 plays General MIDI drums"});
    for (const NoteSetting& note : kNoteSettings) {
        addSetting(registry, {.id = note.id,
                              .title = note.title,
                              .type = SettingType::Integer,
                              .defaultValue = engine::Metronome{}.notes[static_cast<std::size_t>(note.voice)],
                              .minimum = 0,
                              .maximum = kMaxNote,
                              .hint = note.hint});
    }
}

void registerActions(Registry& registry, const ProjectController& controller, Nav& nav) {
    const auto set = [&registry, &controller](const char* id, const Json& value) {
        registry.setSetting(id, value);
        return metronomeState(registry, controller);
    };
    registry.addStateProvider("metronome", [&registry, &controller] { return metronomeState(registry, controller); });
    registry.addAction(
        {.id = "metronome.toggle",
         .title = "Turn metronome on or off",
         .aliases = {"click", "metronome"},
         .category = "Metronome",
         .shortcut = "K",
         .run = [set, &registry](const Json&) { return set(kEnabled, !registry.settingValue(kEnabled).get<bool>()); },
         .menus = {kTransportMenu, kMetronomeMenu}});
    registry.addAction(
        {.id = "metronome.setMode",
         .title = "Metronome sound",
         .aliases = {"click sound", "drum groove", "midi click"},
         .category = "Metronome",
         .schema = {{"required", {"mode"}}, {"properties", {{"mode", {{"type", "string"}, {"enum", ids(kModes)}}}}}},
         .run = [set](const Json& params) { return set(kMode, param<std::string>(params, "mode")); },
         .menus = {kMetronomeMenu},
         .submenu = [] { return choices(kModes, "mode"); }});
    registry.addAction(
        {.id = "metronome.setPattern",
         .title = "Groove pattern",
         .aliases = {"drum beat", "drum pattern"},
         .category = "Metronome",
         .schema = {{"required", {"pattern"}},
                    {"properties", {{"pattern", {{"type", "string"}, {"enum", ids(kPatterns)}}}}}},
         .run = [set](const Json& params) { return set(kPattern, param<std::string>(params, "pattern")); },
         .menus = {kMetronomeMenu},
         .submenu = [] { return choices(kPatterns, "pattern"); }});
    registry.addAction(
        {.id = "metronome.setCountIn",
         .title = "Count-in",
         .aliases = {"count in bars", "pre-roll"},
         .category = "Metronome",
         .schema = {{"required", {"bars"}},
                    {"properties",
                     {{"bars", {{"type", "integer"}, {"minimum", 0}, {"maximum", engine::kMaxCountInBars}}}}}},
         .run = [set](const Json& params) { return set(kCountIn, param<int>(params, "bars")); },
         .menus = {kMetronomeMenu},
         .submenu =
             [] {
                 return std::views::iota(0, engine::kMaxCountInBars + 1) | std::views::transform([](int bars) {
                            return MenuChoice{
                                .id = std::to_string(bars), .title = countInTitle(bars), .params = {{"bars", bars}}};
                        }) |
                        std::ranges::to<std::vector>();
             }});
    registry.addAction({.id = "metronome.showMenu",
                        .title = "Show or hide metronome menu",
                        .aliases = {"metronome options"},
                        .category = "Metronome",
                        .run = [&nav](const Json&) {
                            nav.setMetronomeMenuOpen(!nav.metronomeMenuOpen());
                            return Json{{"metronomeMenuOpen", nav.metronomeMenuOpen()}};
                        }});
    registry.addAction({.id = "metronome.setVolume",
                        .title = "Set metronome volume",
                        .aliases = {"click volume", "metronome level"},
                        .category = "Metronome",
                        .schema = {{"required", {"db"}}, {"properties", {{"db", {{"type", "number"}}}}}},
                        .run = [set](const Json& params) { return set(kVolume, param<double>(params, "db")); }});
}

}  // namespace

void registerMetronome(Registry& registry, ProjectController& controller, Nav& nav) {
    registerSettings(registry, controller, nav);
    registerActions(registry, controller, nav);
    QObject::connect(&registry, &Registry::settingsChanged, &controller,
                     [&registry, &controller] { controller.setMetronome(metronomeFromSettings(registry)); });
    controller.setMetronome(metronomeFromSettings(registry));
}

}  // namespace emerick::app
