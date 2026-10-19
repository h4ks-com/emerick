#include "emerick/yamaha/plugin.hpp"

#include <QtLogging>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/core_actions.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/yamaha/profile.hpp"
#include "emerick/yamaha/render.hpp"
#include "registration.hpp"

namespace emerick::yamaha {

namespace {

constexpr int kMidiChannels = 16;

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

int outChannel(const app::Registry& registry, const model::Track& track, const char* setting) {
    return track.midiOut.channel != 0 ? track.midiOut.channel : registry.settingValue(setting).get<int>();
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

// A damaged project still opens, so a track whose data we cannot read plays nothing.
template <class Event>
std::vector<Event> playableEvents(const model::Track& track) {
    try {
        return eventsOf<Event>(track.data);
    } catch (const app::AppError& error) {
        qWarning("%s on %s", error.what(), track.name.c_str());
        return {};
    }
}

void addSetting(app::Registry& registry, app::Setting setting) {
    setting.category = "Settings / Yamaha";
    setting.location = "settings.yamaha";
    registry.addSetting(std::move(setting));
}

app::Json midiOutputs(const app::ProjectController& controller) {
    return controller.engine().midiPorts() |
           std::views::filter([](const engine::MidiPortInfo& port) { return !port.isInput; }) |
           std::views::transform(
               [](const engine::MidiPortInfo& port) { return app::Json{{"id", port.id}, {"name", port.name}}; }) |
           std::ranges::to<app::Json::array_t>();
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

ChartOptions chartOptions(const app::Registry& registry, int shiftBars) {
    return {.shiftBars = shiftBars,
            .snapToBeat = registry.settingValue(kSnapSetting).get<bool>(),
            .anticipation = registry.settingValue(kAnticipationSetting).get<bool>()};
}

void registerSettings(app::Registry& registry, app::ProjectController& controller, app::Nav& nav) {
    app::addSettingsSection(registry, nav, {.key = "yamaha", .title = "Yamaha Arranger", .group = app::kPluginsGroup});
    addSetting(registry, {.id = kChordChannelSetting,
                          .title = "Chord detect channel",
                          .aliases = {"chord channel"},
                          .type = app::SettingType::Integer,
                          .defaultValue = kChordDetectChannel,
                          .minimum = 1,
                          .maximum = kMidiChannels,
                          .hint = "Match the keyboard's chord detect channel"});
    addSetting(registry, {.id = kControllerPortSetting,
                          .title = "External controller port",
                          .aliases = {"section port"},
                          .type = app::SettingType::String,
                          .defaultValue = "Digital Keyboard Port 2",
                          .hint = "Match the keyboard's external controller port",
                          .suggestions = [&controller] { return midiOutputs(controller); }});
    addSetting(registry, {.id = kControllerChannelSetting,
                          .title = "External controller channel",
                          .aliases = {"section channel"},
                          .type = app::SettingType::Integer,
                          .defaultValue = 1,
                          .minimum = 1,
                          .maximum = kMidiChannels});
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
}

void registerTrackKinds(app::Registry& registry) {
    registry.addTrackType(
        {.kind = kChordKind,
         .title = "Chords",
         .icon = std::string(kChordsIcon),
         .laneComponent = "qrc:/qt/qml/Emerick/Yamaha/ChordLane.qml",
         .routes = {.midi = true},
         .toEngine =
             [&registry](const model::Track& track, const app::PlaybackContext& context) {
                 const auto chords = playableEvents<ChordEvent>(track);
                 const core::Ticks end = chords.empty() ? 0 : chords.back().at + context.tempo.ticksPerBar();
                 return withRendered(track, chordNotes(chords, outChannel(registry, track, kChordChannelSetting), end));
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
             }});
    registry.addTrackType(
        {.kind = kStyleKind,
         .title = "Style",
         .icon = std::string(kStyleIcon),
         .laneComponent = "qrc:/qt/qml/Emerick/Yamaha/StyleLane.qml",
         .routes = {.midi = true},
         .toEngine =
             [&registry](const model::Track& track, const app::PlaybackContext&) {
                 return withRendered(track, sectionControls(playableEvents<SectionEvent>(track),
                                                            outChannel(registry, track, kControllerChannelSetting)));
             },
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
             }});
}

void registerWith(app::Registry& registry, app::ProjectController& controller, app::Nav& nav) {
    registerSettings(registry, controller, nav);
    registerTrackKinds(registry);
    registerDeviceProfile(registry);
    registerChartActions(registry, controller);
    registerArrangerActions(registry, controller);
}

}  // namespace emerick::yamaha
