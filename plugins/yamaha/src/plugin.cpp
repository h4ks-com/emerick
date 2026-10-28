#include "emerick/yamaha/plugin.hpp"

#include <QQmlEngine>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "arranger_styles.hpp"
#include "emerick/app/core_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/yamaha/profile.hpp"
#include "emerick/yamaha/render.hpp"
#include "emerick/yamaha/sysex.hpp"
#include "registration.hpp"
#include "style_library.hpp"

namespace emerick::yamaha {

namespace {

std::vector<RawEvent> rawEvents(const engine::RecordedTake& take) {
    return take.midiEvents | std::views::transform([&](const engine::MidiEvent& event) {
               return RawEvent{take.startTick + event.tick, event.bytes};
           }) |
           std::ranges::to<std::vector>();
}

// A take replaces the events it covers and keeps the rest.
template <class Event>
void mergeTake(model::Track& track, std::vector<Event> recorded, core::Ticks from, core::Ticks to) {
    std::vector<Event> events = eventsOf<Event>(track.data);
    std::erase_if(events, [&](const Event& event) { return event.at >= from && event.at < to; });
    for (Event& event : recorded) {
        event.at = std::max(event.at, from);
        events.push_back(std::move(event));
    }
    std::ranges::stable_sort(events, {}, &Event::at);
    track.data = events;
}

engine::Track withRendered(const model::Track& track, const std::vector<RawEvent>& rendered) {
    engine::Track result = app::midiEngineTrack(track);
    result.midiClips.push_back({.start = 0,
                                .events = rendered | std::views::transform([](const RawEvent& event) {
                                              return engine::MidiEvent{event.at, event.bytes};
                                          }) |
                                          std::ranges::to<std::vector>()});
    return result;
}

int buttonChannel(const app::Registry& registry, const model::Track& style) {
    return style.midiOut.channel != 0 ? style.midiOut.channel
                                      : registry.settingValue(kControllerChannelSetting).get<int>();
}

std::vector<app::MidiMessage> toTrackOutput(const std::vector<std::vector<std::uint8_t>>& messages) {
    return messages | std::views::transform([](const std::vector<std::uint8_t>& bytes) {
               return app::MidiMessage{.port = {}, .bytes = bytes};
           }) |
           std::ranges::to<std::vector>();
}

std::optional<app::StartAlignment> styleStart(const model::Track& style, core::Ticks position,
                                              const core::TempoMap& tempo) {
    const auto pattern = patternAt(playableEvents<SectionEvent>(style), position, tempo.ticksPerBar());
    if (!pattern) {
        return std::nullopt;
    }
    return app::StartAlignment{.position = pattern->passStart,
                               .reason = "Style starts at bar " +
                                         std::to_string(tempo.toBarBeatTick(pattern->passStart).bar) + ", " +
                                         sectionName(pattern->section)};
}

// A Style track with an instrument, such as the Arranger, also plays the chords, so its instrument gets them too.
bool playsChords(const model::Track& style, const app::PlaybackContext& context) {
    return style.instrument.has_value() && context.document != nullptr;
}

void addSetting(app::Registry& registry, app::Setting setting) {
    setting.category = "Settings / Yamaha";
    setting.location = "settings.yamaha";
    registry.addSetting(std::move(setting));
}

void registerDeviceProfile(app::Registry& registry) {
    app::MidiDeviceProfile profile{.nameMatch = "Digital Keyboard"};
    for (const Part& part : genos2Parts()) {
        profile.channelNames[part.channel] = std::string(part.name);
        profile.channelIcons[part.channel] = std::string(part.icon);
    }
    registry.addMidiDeviceProfile(std::move(profile));
}

}  // namespace

std::optional<std::vector<SectionEvent>> playingStyle(const model::Document& document) {
    for (const model::Track& track : document.tracks) {
        if (track.kind == kStyleKind && !track.mute && !track.midiOut.portId.empty()) {
            if (auto sections = playableEvents<SectionEvent>(track); !sections.empty()) {
                return sections;
            }
        }
    }
    return std::nullopt;
}

const model::Track* mutedStyle(const model::Document& document) {
    const model::Track* muted = nullptr;
    for (const model::Track& track : document.tracks) {
        if (track.kind == kStyleKind && !track.midiOut.portId.empty()) {
            if (!track.mute) {
                return nullptr;
            }
            muted = &track;
        }
    }
    return muted;
}

std::optional<Chord> chordIn(const model::Track& chords, core::Ticks position) {
    const std::vector<ChordEvent> events = playableEvents<ChordEvent>(chords);
    const auto after = std::ranges::upper_bound(events, position, {}, &ChordEvent::at);
    return after == events.begin() ? std::nullopt : std::optional{std::prev(after)->chord};
}

std::optional<Chord> chordAt(const model::Document& document, core::Ticks position) {
    const auto chords = std::ranges::find(document.tracks, std::string{kChordKind}, &model::Track::kind);
    return chords == document.tracks.end() ? std::nullopt : chordIn(*chords, position);
}

std::vector<ChordEvent> projectChords(const model::Document& document) {
    const auto chords = std::ranges::find(document.tracks, std::string{kChordKind}, &model::Track::kind);
    return chords == document.tracks.end() ? std::vector<ChordEvent>{} : playableEvents<ChordEvent>(*chords);
}

ChartOptions chartOptions(const app::Registry& registry, int shiftBars) {
    return {.shiftBars = shiftBars,
            .snapToBeat = registry.settingValue(kSnapSetting).get<bool>(),
            .anticipation = registry.settingValue(kAnticipationSetting).get<bool>()};
}

void registerSettings(app::Registry& registry, app::ProjectController& controller, app::Nav& nav) {
    app::addSettingsSection(registry, nav, {.key = "yamaha", .title = "Yamaha Arranger", .group = app::kPluginsGroup});
    addSetting(
        registry,
        {.id = kSnapSetting, .title = "Snap chords to beats", .type = app::SettingType::Boolean, .defaultValue = true});
    addSetting(registry, {.id = kAnticipationSetting,
                          .title = "Beat 4 anticipation",
                          .aliases = {"move beat 4 chords to the next bar"},
                          .type = app::SettingType::Boolean,
                          .defaultValue = true});
    addSetting(registry, {.id = kSendVoicesSetting,
                          .title = "Send voices on play",
                          .aliases = {"voice table"},
                          .type = app::SettingType::Boolean,
                          .defaultValue = true});
    addSetting(
        registry,
        {.id = kControllerPortSetting,
         .title = "External Controller port",
         .aliases = {"section buttons port"},
         .type = app::SettingType::String,
         .defaultValue = "",
         .hint = "Pick the port of the keyboard's External Controller, or leave it empty to use the keyboard port. "
                 "Set up arranger routes the Style track to it",
         .suggestions = [&controller] { return app::midiOutputChoices(controller); }});
    addSetting(registry, {.id = kControllerChannelSetting,
                          .title = "External Controller channel",
                          .aliases = {"section buttons channel"},
                          .type = app::SettingType::Integer,
                          .defaultValue = kControllerChannel,
                          .minimum = 1,
                          .maximum = app::kMidiChannels,
                          .hint = "Channel the Style track presses the keyboard's section buttons on"});
    addSetting(registry, {.id = kStyleFoldersSetting,
                          .title = "Style folders",
                          .aliases = {"style library", "sty folders"},
                          .type = app::SettingType::List,
                          .defaultValue = app::Json::array(),
                          .hint = "Folders to list styles from in the built-in arranger's style picker"});
}

void registerTrackKinds(app::Registry& registry, const StyleSync& sync) {
    registry.addTrackType(
        {.kind = kChordKind,
         .title = "Chords",
         .icon = std::string(kChordsIcon),
         .laneComponent = "qrc:/qt/qml/Emerick/Yamaha/ChordLane.qml",
         .routes = {.midi = true},
         .toEngine =
             [](const model::Track& track, const app::PlaybackContext&) {
                 return withRendered(track, chordControls(playableEvents<ChordEvent>(track)));
             },
         .fromTake =
             [&registry](model::Track& track, const engine::RecordedTake& take, const app::TakeContext& context) {
                 Chart chart = chartFromTake(rawEvents(take), context.tempo, chartOptions(registry));
                 mergeTake(track, std::move(chart.chords), take.startTick, context.stopTicks);
             },
         .markers =
             [](const model::Track& track) {
                 return playableEvents<ChordEvent>(track) | std::views::transform([](const ChordEvent& event) {
                            return app::Marker{.at = event.at, .text = event.chord.name()};
                        }) |
                        std::ranges::to<std::vector>();
             },
         .chase =
             [](const model::Track& track, core::Ticks position, const app::PlaybackContext&) {
                 const auto chord = chordIn(track, position);
                 return chord ? toTrackOutput({encodeChordControl(*chord)}) : std::vector<app::MidiMessage>{};
             }});
    registry.addTrackType(
        {.kind = kStyleKind,
         .title = "Style",
         .icon = std::string(kStyleIcon),
         .headerComponent = "qrc:/qt/qml/Emerick/Yamaha/StyleHeader.qml",
         .laneComponent = "qrc:/qt/qml/Emerick/Yamaha/StyleLane.qml",
         .inspectorComponent = "qrc:/qt/qml/Emerick/Yamaha/StyleInspector.qml",
         .routes = {.midi = true},
         .fileDrop = {.extensions = {kStyleExtensions.begin(), kStyleExtensions.end()},
                      .action = "arranger.style.load"},
         .toEngine =
             [&registry](const model::Track& track, const app::PlaybackContext& context) {
                 std::vector<RawEvent> rendered = sectionPresses(
                     playableEvents<SectionEvent>(track), buttonChannel(registry, track), context.tempo.ticksPerBeat());
                 if (playsChords(track, context)) {
                     std::ranges::copy(chordControls(projectChords(*context.document)), std::back_inserter(rendered));
                     std::ranges::stable_sort(rendered, {}, &RawEvent::at);
                 }
                 return withRendered(track, rendered);
             },
         .readsOtherTracks = true,
         .fromTake =
             [&registry](model::Track& track, const engine::RecordedTake& take, const app::TakeContext& context) {
                 Chart chart = chartFromTake(rawEvents(take), context.tempo, chartOptions(registry));
                 mergeTake(track, std::move(chart.sections), take.startTick, context.stopTicks);
             },
         .markers =
             [](const model::Track& track) {
                 return playableEvents<SectionEvent>(track) | std::views::transform([](const SectionEvent& event) {
                            return app::Marker{.at = event.at, .text = sectionName(event)};
                        }) |
                        std::ranges::to<std::vector>();
             },
         .warnings = missingVoices,
         // The Start/Stop button toggles, so we press it only to stop a Style that runs.
         .panic =
             [&registry, &sync](const model::Track& track) {
                 return sync.styleRunning() ? toTrackOutput(buttonPress(kStartStopCc, buttonChannel(registry, track)))
                                            : std::vector<app::MidiMessage>{};
             },
         .holdsStart = [](const model::Document& document) { return mutedStyle(document) != nullptr; },
         .chase =
             [&registry](const model::Track& track, core::Ticks position, const app::PlaybackContext& context) {
                 std::vector<std::vector<std::uint8_t>> messages;
                 if (const auto chord =
                         playsChords(track, context) ? chordAt(*context.document, position) : std::nullopt) {
                     messages.push_back(encodeChordControl(*chord));
                 }
                 if (const auto section = chaseSection(playableEvents<SectionEvent>(track), position)) {
                     std::ranges::move(sectionPress(*section, buttonChannel(registry, track)),
                                       std::back_inserter(messages));
                 }
                 return toTrackOutput(messages);
             },
         .alignStart = [](const model::Track& track, core::Ticks position,
                          const app::PlaybackContext& context) { return styleStart(track, position, context.tempo); }});
}

Listeners::Listeners(app::Registry& registry, app::ProjectController& controller, app::Nav& nav)
    : chordInput(registry, controller, nav),
      keyboardCheck(std::make_unique<KeyboardCheck>(controller)),
      styleSync(std::make_unique<StyleSync>(controller, registry)),
      arrangerStyles(std::make_unique<ArrangerStyles>(controller, registry)) {
    qmlRegisterSingletonInstance("Emerick.Yamaha", 1, 0, "KeyboardCheck", keyboardCheck.get());
    qmlRegisterSingletonInstance("Emerick.Yamaha", 1, 0, "ArrangerStyles", arrangerStyles.get());
}

Listeners::~Listeners() = default;

std::unique_ptr<Listeners> registerWith(app::Registry& registry, app::ProjectController& controller, app::Nav& nav) {
    registerSettings(registry, controller, nav);
    auto listeners = std::make_unique<Listeners>(registry, controller, nav);
    registerTrackKinds(registry, *listeners->styleSync);
    registerDeviceProfile(registry);
    registerChartActions(registry, controller, nav);
    registerArrangerActions(registry, controller, *listeners->keyboardCheck);
    registerTemplateActions(registry);
    registerBuiltinArranger(registry, controller, nav);
    return listeners;
}

}  // namespace emerick::yamaha
