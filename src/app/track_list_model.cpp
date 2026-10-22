#include "emerick/app/track_list_model.hpp"

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <ranges>
#include <span>
#include <utility>
#include <variant>

#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/core/overloaded.hpp"
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
    QVariantList notes;
    for (const model::NoteSpan& note : model::noteSpans(clip)) {
        notes.append(QVariantMap{
            {"at", QVariant::fromValue(note.at)}, {"length", QVariant::fromValue(note.length)}, {"key", note.key}});
    }
    return notes;
}

}  // namespace

QVariantList TrackListModel::clipList(const model::Track& track) const {
    const core::TempoMap& tempo = project_.document().tempo;
    const double rate = controller_.sampleRate();
    QVariantList clips;
    int take = 0;
    for (const model::Clip& clip : track.clips) {
        const core::Ticks start = model::clipStart(clip);
        const core::Ticks end = model::clipEnd(clip);
        QVariantMap entry{{"id", qs(model::clipId(clip).value)},
                          {"take", ++take},
                          {"start", QVariant::fromValue(start)},
                          {"length", QVariant::fromValue(end - start)},
                          {"startText", qs(tempo.toBarBeatTick(start).format())},
                          {"endText", qs(tempo.toBarBeatTick(end).format())},
                          {"lengthText", qs(durationText(end - start, tempo))}};
        std::visit(
            core::Overloaded{[&](const model::AudioClip& audio) {
                                 const std::filesystem::path file{audio.file};
                                 const core::Frames frames =
                                     tempo.ticksToFrames(end, rate) - tempo.ticksToFrames(start, rate);
                                 entry.insert(QVariantMap{
                                     {"type", "audio"},
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
        clips.append(entry);
    }
    return clips;
}

TrackListModel::TrackListModel(ProjectController& controller, const Registry& registry, QObject* parent)
    : QAbstractListModel(parent),
      controller_(controller),
      project_(controller.project()),
      registry_(registry),
      rows_(project_.document().tracks | std::views::transform(&model::Track::id) | std::ranges::to<std::vector>()),
      listener_(project_.addListener([this](const model::Change& change) { changed(change); })) {}

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
            return track.mute;
        case Role::solo:
            return track.solo;
        case Role::armed:
            return track.armed;
        case Role::monitor:
            return track.monitor;
        case Role::gainDb:
            return track.gainDb;
        case Role::pan:
            return track.pan;
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
        case Role::clips:
            return clipList(track);
        case Role::trackData:
            return toVariant(track.data);
        case Role::laneComponent:
            return qs(type != nullptr ? type->laneComponent : std::string{});
        case Role::clipType:
            return qs(clipTypeName(clipTypeOf(type)));
    }
    return {};
}

QHash<int, QByteArray> TrackListModel::roleNames() const {
    static constexpr std::array<std::pair<Role, const char*>, 21> kNames{{{Role::id, "trackId"},
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
                                                                          {Role::clips, "clips"},
                                                                          {Role::trackData, "trackData"},
                                                                          {Role::laneComponent, "laneComponent"},
                                                                          {Role::clipType, "clipType"}}};
    QHash<int, QByteArray> names;
    for (const auto& [role, name] : kNames) {
        names.insert(static_cast<int>(role), name);
    }
    return names;
}

void TrackListModel::changed(const model::Change& change) {
    const auto row = change.track ? std::ranges::find(rows_, *change.track) : rows_.end();
    if (row != rows_.end() && project_.track(*row) != nullptr) {
        const QModelIndex at = index(static_cast<int>(row - rows_.begin()));
        emit dataChanged(at, at);
        return;
    }
    syncRows();
    if (!rows_.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1));
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
