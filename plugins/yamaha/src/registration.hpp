#pragma once

#include <QtLogging>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/yamaha/chart.hpp"
#include "keyboard_check.hpp"
#include "style_sync.hpp"

namespace emerick::yamaha {

inline constexpr const char* kChordKind = "yamaha.chord";
inline constexpr const char* kStyleKind = "yamaha.style";
inline constexpr const char* kYamahaMenu = "Plugins/Yamaha Arranger";
// The menus a right click on a chord or section block opens, which run their actions with that block.
inline constexpr const char* kChordContextMenu = "Chord context";
inline constexpr const char* kSectionContextMenu = "Section context";

inline constexpr const char* kSnapSetting = "yamaha.snapToBeat";
inline constexpr const char* kAnticipationSetting = "yamaha.anticipation";
inline constexpr const char* kSendVoicesSetting = "yamaha.sendVoicesOnPlay";
inline constexpr const char* kControllerPortSetting = "yamaha.controllerPort";
inline constexpr const char* kControllerChannelSetting = "yamaha.controllerChannel";
inline constexpr int kControllerChannel = 16;

// A Chords or Style track keeps its events in data as a JSON array; a new track has no data and no events.
template <class Event>
std::vector<Event> eventsOf(const nlohmann::json& data) {
    if (data.is_null()) {
        return {};
    }
    try {
        auto events = data.get<std::vector<Event>>();
        std::ranges::stable_sort(events, {}, &Event::at);
        return events;
    } catch (const nlohmann::json::exception& error) {
        throw app::AppError(std::string{"the track data is damaged: "} + error.what());
    } catch (const std::invalid_argument& error) {
        throw app::AppError(std::string{"the track data is damaged: "} + error.what());
    }
}

inline void addTrack(app::ProjectController& controller, model::Track track) {
    track.color = static_cast<int>(controller.project().document().tracks.size() % app::kTrackColors);
    controller.project().addTrack(std::move(track));
}

// The port and channel the first Style track presses the keyboard's section buttons on.
inline std::optional<model::MidiRoute> styleButtons(const model::Document& document) {
    const auto style = std::ranges::find(document.tracks, std::string{kStyleKind}, &model::Track::kind);
    return style == document.tracks.end() || style->midiOut.portId.empty() ? std::nullopt
                                                                           : std::optional{style->midiOut};
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

// A Style track plays when it is unmuted, routed to an output and holds sections.
std::optional<std::vector<SectionEvent>> playingStyle(const model::Document& document);

// The last routed Style track when every routed Style track is muted, else null.
const model::Track* mutedStyle(const model::Document& document);
std::optional<Chord> chordIn(const model::Track& chords, core::Ticks position);
// We read the first Chords track.
std::optional<Chord> chordAt(const model::Document& document, core::Ticks position);
std::vector<ChordEvent> projectChords(const model::Document& document);

ChartOptions chartOptions(const app::Registry& registry, int shiftBars = 0);

void registerSettings(app::Registry& registry, app::ProjectController& controller, app::Nav& nav);
void registerTrackKinds(app::Registry& registry, const StyleSync& sync);
void registerChartActions(app::Registry& registry, app::ProjectController& controller, app::Nav& nav);
void registerArrangerActions(app::Registry& registry, app::ProjectController& controller, KeyboardCheck& check);
void registerTemplateActions(app::Registry& registry);
// A warning on each part track that plays General MIDI because no installed voice matches the style's voice.
std::vector<engine::SessionError> missingVoices(const model::Track& style, const model::Document& document);
// The built-in arranger: its set-up and the style actions of a Style track that plays the Arranger device.
void registerBuiltinArranger(app::Registry& registry, app::ProjectController& controller, app::Nav& nav);

}  // namespace emerick::yamaha
