#include "emerick/app/track_list_model.hpp"

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iterator>
#include <ranges>
#include <span>
#include <utility>
#include <variant>

#include "emerick/app/automation_params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/core/overloaded.hpp"
#include "emerick/model/automation.hpp"
#include "emerick/model/clip_edit.hpp"

namespace emerick::app {

namespace {

QVariantMap audioRoute(const model::AudioRoute& route) {
    QVariantList channels;
    std::ranges::copy(route.channels, std::back_inserter(channels));
    return {{"device", qs(route.deviceId)}, {"channels", channels}};
}

QVariantMap midiRoute(const model::MidiRoute& route) {
    return {{"port", qs(route.portId)}, {"channel", route.channel}, {"thru", route.thru}, {"sysex", route.sysex}};
}

// QML compares this with a clip's type.
const char* clipTypeName(ClipType type) {
    constexpr std::array kNames{"", "audio", "midi"};
    return kNames.at(static_cast<std::size_t>(type));
}

QVariantList noteList(const model::MidiClip& clip) {
    return clip.notes | std::views::transform([](const model::Note& note) {
               return QVariant{QVariantMap{{"id", qs(note.id.value)},
                                           {"pitch", note.pitch},
                                           {"velocity", note.velocity},
                                           {"start", QVariant::fromValue(note.start)},
                                           {"length", QVariant::fromValue(note.length)},
                                           {"channel", note.channel}}};
           }) |
           std::ranges::to<QVariantList>();
}

QVariant pointEntry(const model::AutomationPoint& point) {
    return QVariantMap{{"id", qs(point.id.value)},
                       {"tick", QVariant::fromValue(point.tick)},
                       {"value", point.value},
                       {"curve", qs(std::string{core::automationCurveId(point.curve)})}};
}

QVariantList laneList(const model::Track& track) {
    return track.automation | std::views::transform([](const model::AutomationLane& lane) {
               return QVariant{QVariantMap{
                   {"param", qs(lane.param)},
                   {"points", lane.points | std::views::transform(pointEntry) | std::ranges::to<QVariantList>()}}};
           }) |
           std::ranges::to<QVariantList>();
}

}  // namespace

QVariantMap TrackListModel::clipEntry(const model::Clip& clip, int take) const {
    const core::TempoMap& tempo = project_.document().tempo;
    const double rate = controller_.sampleRate();
    const core::Ticks start = model::clipStart(clip);
    const core::Ticks end = model::clipEnd(clip);
    QVariantMap entry{{"id", qs(model::clipId(clip).value)},
                      {"take", take},
                      {"start", QVariant::fromValue(start)},
                      {"length", QVariant::fromValue(end - start)},
                      {"startText", qs(tempo.toBarBeatTick(start).format())},
                      {"endText", qs(tempo.toBarBeatTick(end).format())},
                      {"lengthText", qs(durationText(end - start, tempo))}};
    std::visit(core::Overloaded{
                   [&](const model::AudioClip& audio) {
                       const std::filesystem::path file{audio.file};
                       const core::Frames frames = tempo.ticksToFrames(end, rate) - tempo.ticksToFrames(start, rate);
                       entry.insert(
                           QVariantMap{{"type", "audio"},
                                       {"fadeIn", QVariant::fromValue(audio.fadeIn)},
                                       {"fadeOut", QVariant::fromValue(audio.fadeOut)},
                                       {"fadeInShape", qs(std::string{core::fadeShapeId(audio.fadeInShape)})},
                                       {"fadeOutShape", qs(std::string{core::fadeShapeId(audio.fadeOutShape)})},
                                       {"fadeInCurve", audio.fadeInCurve},
                                       {"fadeOutCurve", audio.fadeOutCurve},
                                       {"gainDb", audio.gainDb},
                                       {"file", qs((file.is_absolute() ? file : controller_.folder() / file).string())},
                                       {"sourceOffset", QVariant::fromValue(audio.sourceOffset)},
                                       {"frames", QVariant::fromValue(frames)}});
                   },
                   [&](const model::MidiClip& midi) {
                       entry.insert(QVariantMap{{"type", "midi"}, {"notes", noteList(midi)}});
                   }},
               clip);
    return entry;
}

QVariantList TrackListModel::clipList(const model::Track& track) const {
    QVariantList clips;
    int take = 0;
    for (const model::Clip& clip : track.clips) {
        clips.append(clipEntry(clip, ++take));
    }
    return clips;
}

QVariantMap TrackListModel::clip(const QString& id) const {
    const auto found = project_.findClip(model::ClipId{id.toStdString()});
    if (!found) {
        return {};
    }
    const auto& clips = found->track->clips;
    const auto take = std::ranges::find(clips, found->clip, [](const model::Clip& clip) { return &clip; });
    QVariantMap entry = clipEntry(*found->clip, static_cast<int>(take - clips.begin()) + 1);
    entry.insert({{"trackId", qs(found->track->id.value)},
                  {"trackName", qs(found->track->name)},
                  {"colorIndex", found->track->color}});
    return entry;
}

bool TrackListModel::sameAcross(const QStringList& ids, const QString& role) const {
    const int key = roleNames().key(role.toUtf8(), -1);
    const auto values =
        ids | std::views::transform([&](const QString& id) {
            const auto row = std::ranges::find(rows_, model::TrackId{id.toStdString()});
            return row == rows_.end() ? QVariant{} : data(index(static_cast<int>(row - rows_.begin())), key);
        }) |
        std::ranges::to<std::vector>();
    return std::ranges::adjacent_find(values, std::ranges::not_equal_to{}) == values.end();
}

QString TrackListModel::trackName(const model::TrackId& id) const {
    const model::Track* track = project_.track(id);
    return track != nullptr ? qs(track->name) : QString{};
}

QVariantList TrackListModel::sendList(const model::Track& track) const {
    QVariantList sends;
    for (const model::Send& send : track.sends) {
        sends.append(
            QVariantMap{{"bus", qs(send.bus.value)},
                        {"busName", trackName(send.bus)},
                        {"db", controller_.automatedValue(track, sendParamId(send.bus)).value_or(send.levelDb)},
                        {"pre", send.pre},
                        {"enabled", send.enabled}});
    }
    return sends;
}

QVariantList TrackListModel::paramList(const model::Track& track) const {
    return automationParams(registry_, project_.document(), track) |
           std::views::transform([](const AutomationParam& param) {
               return QVariant{QVariantMap{{"id", qs(param.id)},
                                           {"title", qs(param.title)},
                                           {"minimum", param.minimum},
                                           {"maximum", param.maximum},
                                           {"value", param.value},
                                           {"toggle", param.toggle},
                                           {"unit", qs(param.unit)}}};
           }) |
           std::ranges::to<QVariantList>();
}

QVariantMap TrackListModel::insert(const QString& id) const {
    const model::InsertId insertId{id.toStdString()};
    const model::Track* track = model::trackWithInsert(project_.document(), insertId);
    if (track == nullptr) {
        return {};
    }
    Json state = insertState(*std::ranges::find(track->inserts, insertId, &model::Insert::id));
    state["track"] = track->id.value;
    state["trackName"] = track->name;
    return toVariant(state).toMap();
}

bool TrackListModel::isBus(const model::TrackId& id) const {
    const model::Track* track = project_.track(id);
    const TrackType* type = track != nullptr ? registry_.trackType(track->kind) : nullptr;
    return type != nullptr && type->routes.bus;
}

TrackListModel::TrackListModel(ProjectController& controller, const Registry& registry, QObject* parent)
    : QAbstractListModel(parent),
      controller_(controller),
      project_(controller.project()),
      registry_(registry),
      rows_(project_.document().tracks | std::views::transform(&model::Track::id) | std::ranges::to<std::vector>()),
      listener_(project_.addListener([this](const model::Change& change) { changed(change); })) {
    connect(&controller_, &ProjectController::transportChanged, this, &TrackListModel::transportMoved);
}

TrackListModel::~TrackListModel() { project_.removeListener(listener_); }

int TrackListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant TrackListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= rows_.size()) {
        return {};
    }
    const model::Track* found = project_.track(rows_[static_cast<std::size_t>(index.row())]);
    if (found == nullptr) {
        return {};
    }
    const model::Track& track = *found;
    const TrackType* type = registry_.trackType(track.kind);
    const RouteSections routes = type != nullptr ? type->routes : RouteSections{};
    switch (static_cast<Role>(role)) {
        case Role::id:
            return qs(track.id.value);
        case Role::name:
            return qs(track.name);
        case Role::kind:
            return qs(track.kind);
        case Role::icon:
            return qs(track.icon);
        case Role::colorIndex:
            return track.color;
        case Role::mute:
            return controller_.automatedValue(track, "mute")
                .transform([](double value) { return value >= 0.5; })
                .value_or(track.mute);
        case Role::solo:
            return track.solo;
        case Role::armed:
            return track.armed;
        case Role::monitor:
            return track.monitor;
        case Role::gainDb:
            return controller_.automatedValue(track, "gain").value_or(track.gainDb);
        case Role::pan:
            return controller_.automatedValue(track, "pan").value_or(track.pan);
        case Role::audioIn:
            return audioRoute(track.audioIn);
        case Role::audioOut:
            return audioRoute(track.audioOut);
        case Role::midiIn:
            return midiRoute(track.midiIn);
        case Role::midiOut:
            return midiRoute(track.midiOut);
        case Role::audioRoutes:
            return routes.audio;
        case Role::midiRoutes:
            return routes.midi;
        case Role::busRoutes:
            return routes.bus;
        case Role::sends:
            return sendList(track);
        case Role::inserts:
            return toVariant(track.inserts | std::views::transform(insertState) | std::ranges::to<Json::array_t>());
        case Role::outputBusName:
            return trackName(track.outputBus);
        case Role::clips:
            return clipList(track);
        case Role::trackData:
            return toVariant(track.data);
        case Role::laneComponent:
            return qs(type != nullptr ? type->laneComponent : std::string{});
        case Role::clipType:
            return qs(clipTypeName(clipTypeOf(type)));
        case Role::automationMode:
            return qs(std::string{model::automationModeId(track.automationMode)});
        case Role::automation:
            return laneList(track);
        case Role::automationParams:
            return paramList(track);
    }
    return {};
}

QHash<int, QByteArray> TrackListModel::roleNames() const {
    static constexpr std::array<std::pair<Role, const char*>, 28> kNames{
        {{Role::id, "trackId"},
         {Role::name, "name"},
         {Role::kind, "kind"},
         {Role::icon, "icon"},
         {Role::colorIndex, "colorIndex"},
         {Role::mute, "mute"},
         {Role::solo, "solo"},
         {Role::armed, "armed"},
         {Role::monitor, "monitor"},
         {Role::gainDb, "gainDb"},
         {Role::pan, "pan"},
         {Role::audioIn, "audioIn"},
         {Role::audioOut, "audioOut"},
         {Role::midiIn, "midiIn"},
         {Role::midiOut, "midiOut"},
         {Role::audioRoutes, "audioRoutes"},
         {Role::midiRoutes, "midiRoutes"},
         {Role::busRoutes, "busRoutes"},
         {Role::sends, "sends"},
         {Role::inserts, "inserts"},
         {Role::outputBusName, "outputBusName"},
         {Role::clips, "clips"},
         {Role::trackData, "trackData"},
         {Role::laneComponent, "laneComponent"},
         {Role::clipType, "clipType"},
         {Role::automationMode, "automationMode"},
         {Role::automation, "automation"},
         {Role::automationParams, "automationParams"}}};
    QHash<int, QByteArray> names;
    for (const auto& [role, name] : kNames) {
        names.insert(static_cast<int>(role), name);
    }
    return names;
}

void TrackListModel::changed(const model::Change& change) {
    ++revision_;
    emit revisionChanged();
    const auto row = change.track ? std::ranges::find(rows_, *change.track) : rows_.end();
    // Other rows show a bus's name beside their sends, so a bus edit refreshes every row.
    if (row != rows_.end() && project_.track(*row) != nullptr && !isBus(*row)) {
        const QModelIndex at = index(static_cast<int>(row - rows_.begin()));
        emit dataChanged(at, at);
        return;
    }
    syncRows();
    if (!rows_.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1));
    }
}

void TrackListModel::transportMoved() {
    for (std::size_t row = 0; row < rows_.size(); ++row) {
        const model::Track* track = project_.track(rows_[row]);
        if (track != nullptr && !track->automation.empty() && track->automationMode != model::AutomationMode::off) {
            const QModelIndex at = index(static_cast<int>(row));
            emit dataChanged(at, at,
                             {static_cast<int>(Role::gainDb), static_cast<int>(Role::pan), static_cast<int>(Role::mute),
                              static_cast<int>(Role::sends)});
        }
    }
}

void TrackListModel::syncRows() {
    const auto& tracks = project_.document().tracks;
    for (auto row = static_cast<std::ptrdiff_t>(rows_.size()) - 1; row >= 0; --row) {
        if (project_.track(rows_[static_cast<std::size_t>(row)]) == nullptr) {
            beginRemoveRows({}, static_cast<int>(row), static_cast<int>(row));
            rows_.erase(rows_.begin() + row);
            endRemoveRows();
        }
    }
    const auto held = [this](const model::TrackId& id) { return std::ranges::contains(rows_, id); };
    for (std::size_t row = 0; row < tracks.size(); ++row) {
        const auto at = rows_.begin() + static_cast<std::ptrdiff_t>(row);
        if (row < rows_.size() && *at == tracks[row].id) {
            continue;
        }
        if (const auto from = std::find(at, rows_.end(), tracks[row].id); from != rows_.end()) {
            beginMoveRows({}, static_cast<int>(from - rows_.begin()), static_cast<int>(from - rows_.begin()), {},
                          static_cast<int>(row));
            std::rotate(at, from, std::next(from));
            endMoveRows();
            continue;
        }
        std::size_t end = row + 1;
        while (end < tracks.size() && !held(tracks[end].id)) {
            ++end;
        }
        beginInsertRows({}, static_cast<int>(row), static_cast<int>(end - 1));
        const auto added = std::span(tracks).subspan(row, end - row) | std::views::transform(&model::Track::id);
        rows_.insert(at, added.begin(), added.end());
        endInsertRows();
        row = end - 1;
    }
}

}  // namespace emerick::app
