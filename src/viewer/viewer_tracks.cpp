#include "viewer_tracks.hpp"

#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "emerick/app/project_view.hpp"
#include "emerick/app/track_params.hpp"
#include "emerick/app/track_routes.hpp"
#include "emerick/app/track_rows.hpp"
#include "emerick/app/variant.hpp"
#include "emerick/model/automation.hpp"
#include "emerick/yamaha/arranger_state.hpp"
#include "emerick/yamaha/lane_layout.hpp"
#include "emerick/yamaha/profile.hpp"
#include "gm_sounds.hpp"

namespace emerick::viewer {

namespace {

// The document keeps no sample rate, so we draw audio clips at the rate the service renders at.
constexpr double kSampleRate = 48000.0;

// A lane of a plugin insert's param, whose range only the live plugin knows, spans the values its points hold.
app::AutomationParam spanOfPoints(const model::AutomationLane& lane) {
    const auto values = lane.points | std::views::transform(&model::AutomationPoint::value);
    const auto [low, high] =
        values.empty() ? std::pair{0.0, 1.0} : std::pair{std::ranges::min(values), std::ranges::max(values)};
    return {.id = lane.param,
            .title = lane.param,
            .minimum = std::min(low, 0.0),
            .maximum = high > low ? high : low + 1.0,
            .value = low,
            .toggle = false,
            .target = core::AutomationTarget::insert,
            .bus = {},
            .unit = {},
            .insert = {},
            .insertParam = 0,
            .plugin = true,
            .scale = app::LaneScale::linear};
}

// The viewer cannot tell which tracks play audio, so it lists params only for tracks that hold lanes.
std::vector<app::AutomationParam> laneParams(const model::Document& document, const model::Track& track) {
    if (track.automation.empty()) {
        return {};
    }
    std::vector<app::AutomationParam> params = app::trackParams(document, track);
    for (const model::AutomationLane& lane : track.automation) {
        if (!std::ranges::contains(params, lane.param, &app::AutomationParam::id)) {
            params.push_back(spanOfPoints(lane));
        }
    }
    return params;
}

// The viewer reads track types from the app's own kinds; a plugin's kind plays audio once it holds an instrument.
app::RouteSections routesOf(const model::Track& track) {
    return app::coreRoutes(track.kind).value_or(app::RouteSections{.midi = track.instrument.has_value()});
}

app::RowContext rowContext(const model::Document& document) {
    return {.tempo = document.tempo, .sampleRate = kSampleRate, .folder = {}};
}

yamaha::ArrangerState arrangerStateOf(const model::Track& track) {
    if (!track.instrument || track.instrument->pluginId != app::kArrangerPluginId) {
        return {};
    }
    return yamaha::parseArrangerState(std::string{track.instrument->state.begin(), track.instrument->state.end()});
}

std::optional<std::vector<yamaha::LaneBlock>> laneBlocksOf(const model::Document& document, const model::Track& track) {
    return yamaha::laneBlocks(track.kind, track.data, arrangerStateOf(track).autoFill, document.tempo.ticksPerBar());
}

// A part track that the arranger plays live holds no clips, since only a frozen part keeps its notes.
const model::Track* playingStyle(const model::Document& document, const model::Track& track) {
    if (!track.clips.empty()) {
        return nullptr;
    }
    const model::Track* source = model::findTrack(document, track.midiIn.sourceTrack);
    return source != nullptr && source->kind == yamaha::kStyleKind ? source : nullptr;
}

bool playsDrums(int channel) { return channel == 9 || channel == 10; }

// A project file can hold anything, so we read a field only when it has the type we expect.
std::string stringField(const app::Json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

std::size_t countField(const app::Json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_number_unsigned() ? found->get<std::size_t>() : 0;
}

std::string partVoice(const model::Track& track) {
    if (!track.instrument) {
        return {};
    }
    const app::Json state =
        app::Json::parse(track.instrument->state.begin(), track.instrument->state.end(), nullptr, false);
    if (!state.is_object()) {
        return track.instrument->name;
    }
    if (track.instrument->pluginId == app::kVoicesPluginId && state.contains("voice") &&
        state.at("voice").is_object()) {
        const app::Json& voice = state.at("voice");
        const std::string name = stringField(voice, "name");
        return name.empty() ? std::format("voice {}/{}/{}", countField(voice, "msb"), countField(voice, "lsb"),
                                          countField(voice, "program") + 1)
                            : name;
    }
    if (track.instrument->pluginId == app::kGeneralMidiPluginId) {
        if (playsDrums(track.midiIn.channel)) {
            const std::size_t kit = countField(state, "drumKit");
            return kit < gm::kDrumKits.size() ? std::format("{} kit", gm::kDrumKits.at(kit).name) : std::string{};
        }
        const std::size_t sound = countField(state, "sound");
        return sound < gm::kSounds.size() ? std::string{gm::kSounds.at(sound)} : std::string{};
    }
    return track.instrument->name;
}

// The one block an empty part track draws over the song: its part, the voice it plays and the style that plays it.
yamaha::LaneBlock partBlock(const model::Document& document, const model::Track& part, const model::Track& style) {
    core::Ticks from = 0;
    core::Ticks to = yamaha::kLastBlockTicks;
    bool any = false;
    for (const model::Track& track : document.tracks) {
        for (const yamaha::LaneBlock& block :
             laneBlocksOf(document, track).value_or(std::vector<yamaha::LaneBlock>{})) {
            from = any ? std::min(from, block.at) : block.at;
            to = any ? std::max(to, block.end) : block.end;
            any = true;
        }
    }
    const std::string name =
        std::ranges::contains(yamaha::genos2Parts(), part.midiIn.channel, &yamaha::Part::channel)
            ? std::string{yamaha::genos2Parts().at(static_cast<std::size_t>(part.midiIn.channel - 1)).name}
            : part.name;
    const std::string voice = partVoice(part);
    const std::string styleName = std::filesystem::path(arrangerStateOf(style).style).stem().string();
    return {.at = from,
            .end = to,
            .label = std::format("{}{} played by the {}style", name, voice.empty() ? "" : ": " + voice,
                                 styleName.empty() ? "" : styleName + " "),
            .gap = true,
            .colorIndex = 0,
            .patternBars = 0};
}

}  // namespace

ViewerTracks::ViewerTracks(const ViewerProject& project, QObject* parent)
    : QAbstractListModel(parent), project_(project) {
    connect(&project_, &ViewerProject::projectChanged, this, [this] {
        beginResetModel();
        endResetModel();
        ++revision_;
        emit revisionChanged();
    });
}

int ViewerTracks::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(project_.document().tracks.size());
}

QVariant ViewerTracks::data(const QModelIndex& index, int role) const {
    const model::Document& document = project_.document();
    if (!index.isValid() || index.row() < 0 || std::cmp_greater_equal(index.row(), document.tracks.size())) {
        return {};
    }
    const model::Track& track = document.tracks[static_cast<std::size_t>(index.row())];
    const app::RouteSections routes = routesOf(track);
    switch (static_cast<Role>(role)) {
        case Role::id:
            return app::qs(track.id.value);
        case Role::name:
            return app::qs(track.name);
        case Role::icon:
            return app::qs(track.icon);
        case Role::colorIndex:
            return track.color;
        case Role::mute:
            return track.mute;
        case Role::solo:
            return track.solo;
        case Role::armed:
        case Role::monitor:
            return false;
        case Role::gainDb:
            return track.gainDb;
        case Role::trimDb:
            return track.trimDb;
        case Role::pan:
            return track.pan;
        case Role::audioOut:
            return app::audioRouteEntry(track.audioOut);
        case Role::midiOut:
            return app::midiRouteEntry(track.midiOut);
        case Role::audioRoutes:
            return routes.audio;
        case Role::midiRoutes:
            return routes.midi;
        case Role::busRoutes:
            return routes.bus;
        case Role::playsAudio:
            return app::playsAudio(routes, track);
        case Role::instrumentId:
            return app::qs(track.instrument ? track.instrument->pluginId : std::string{});
        case Role::instrumentName:
            return app::qs(track.instrument ? track.instrument->name : std::string{});
        case Role::instrument:
            return app::instrumentEntry(track);
        case Role::sends:
            return app::sendList(track, document, &model::Send::levelDb);
        case Role::inserts:
            return app::insertList(track);
        case Role::outputBusName: {
            const model::Track* bus = model::findTrack(document, track.outputBus);
            return bus != nullptr ? app::qs(bus->name) : QString{};
        }
        case Role::clips:
            return app::clipList(track, rowContext(document));
        case Role::trackData:
            if (const auto blocks = laneBlocksOf(document, track)) {
                return app::toVariant(*blocks);
            }
            if (const model::Track* style = playingStyle(document, track)) {
                return app::toVariant(std::vector{partBlock(document, track, *style)});
            }
            return QVariantMap{};
        case Role::takePreview:
            return QVariantMap{};
        case Role::laneComponent:
            return yamaha::drawsBlocks(track.kind) || playingStyle(document, track) != nullptr
                       ? QStringLiteral("qrc:/qt/qml/Emerick/Viewer/BlockRow.qml")
                       : QString{};
        case Role::headerComponent:
        case Role::clipType:
            return QString{};
        case Role::fileDrop:
            return QVariant{};
        case Role::automationMode:
            return app::qs(std::string{model::automationModeId(track.automationMode)});
        case Role::automation:
            return app::laneList(track);
        case Role::automationParams:
            return app::paramList(laneParams(document, track));
    }
    return {};
}

QHash<int, QByteArray> ViewerTracks::roleNames() const {
    static constexpr std::array<std::pair<Role, const char*>, 33> kNames{{{Role::id, "trackId"},
                                                                          {Role::name, "name"},
                                                                          {Role::icon, "icon"},
                                                                          {Role::colorIndex, "colorIndex"},
                                                                          {Role::mute, "mute"},
                                                                          {Role::solo, "solo"},
                                                                          {Role::armed, "armed"},
                                                                          {Role::monitor, "monitor"},
                                                                          {Role::gainDb, "gainDb"},
                                                                          {Role::trimDb, "trimDb"},
                                                                          {Role::pan, "pan"},
                                                                          {Role::audioOut, "audioOut"},
                                                                          {Role::midiOut, "midiOut"},
                                                                          {Role::audioRoutes, "audioRoutes"},
                                                                          {Role::midiRoutes, "midiRoutes"},
                                                                          {Role::busRoutes, "busRoutes"},
                                                                          {Role::playsAudio, "playsAudio"},
                                                                          {Role::instrumentId, "instrumentId"},
                                                                          {Role::instrumentName, "instrumentName"},
                                                                          {Role::instrument, "instrument"},
                                                                          {Role::sends, "sends"},
                                                                          {Role::inserts, "inserts"},
                                                                          {Role::outputBusName, "outputBusName"},
                                                                          {Role::clips, "clips"},
                                                                          {Role::trackData, "trackData"},
                                                                          {Role::laneComponent, "laneComponent"},
                                                                          {Role::headerComponent, "headerComponent"},
                                                                          {Role::fileDrop, "fileDrop"},
                                                                          {Role::clipType, "clipType"},
                                                                          {Role::automationMode, "automationMode"},
                                                                          {Role::automation, "automation"},
                                                                          {Role::automationParams, "automationParams"},
                                                                          {Role::takePreview, "takePreview"}}};
    QHash<int, QByteArray> names;
    for (const auto& [role, name] : kNames) {
        names.insert(static_cast<int>(role), name);
    }
    return names;
}

QVariantMap ViewerTracks::clip(const QString& id) const {
    const model::Document& document = project_.document();
    const auto found = model::findClip(document, model::ClipId{id.toStdString()});
    if (!found) {
        return {};
    }
    const auto& clips = found->track->clips;
    const auto take = std::ranges::find(clips, found->clip, [](const model::Clip& clip) { return &clip; });
    QVariantMap entry = app::clipEntry(*found->clip, static_cast<int>(take - clips.begin()) + 1, rowContext(document));
    entry.insert({{"trackId", app::qs(found->track->id.value)},
                  {"trackName", app::qs(found->track->name)},
                  {"colorIndex", found->track->color}});
    return entry;
}

const model::MidiClip* ViewerTracks::midiClip(const QString& id) const {
    const auto found = model::findClip(project_.document(), model::ClipId{id.toStdString()});
    return found ? std::get_if<model::MidiClip>(found->clip) : nullptr;
}

QList<double> ViewerTracks::controlPoints(const QString& clip, const QString& lane) const {
    const model::MidiClip* midi = midiClip(clip);
    return midi == nullptr ? QList<double>{} : app::controlPointList(*midi, lane);
}

QVariantList ViewerTracks::laneChoices(const QString& clip) const {
    const model::MidiClip* midi = midiClip(clip);
    return midi == nullptr ? QVariantList{} : app::toVariant(app::laneChoices(*midi)).toList();
}

}  // namespace emerick::viewer
