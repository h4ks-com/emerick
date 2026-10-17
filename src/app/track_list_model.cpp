#include "emerick/app/track_list_model.hpp"

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <utility>
#include <variant>

#include "emerick/app/project_state.hpp"
#include "emerick/core/overloaded.hpp"

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

QVariantMap clipEntry(const model::Clip& clip) {
    const auto common = [](const auto& value, const char* type) {
        return QVariantMap{{"id", qs(value.id.value)},
                           {"type", type},
                           {"start", QVariant::fromValue(value.start)},
                           {"length", QVariant::fromValue(value.length)}};
    };
    return std::visit(core::Overloaded{[&](const model::AudioClip& audio) { return common(audio, "audio"); },
                                       [&](const model::MidiClip& midi) { return common(midi, "midi"); }},
                      clip);
}

QVariantList clipList(const model::Track& track) {
    QVariantList clips;
    std::ranges::transform(track.clips, std::back_inserter(clips), clipEntry);
    return clips;
}

}  // namespace

TrackListModel::TrackListModel(model::Project& project, const Registry& registry, QObject* parent)
    : QAbstractListModel(parent),
      project_(project),
      registry_(registry),
      listener_(project.addListener([this](const model::Change& change) { changed(change); })) {}

TrackListModel::~TrackListModel() { project_.removeListener(listener_); }

int TrackListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(project_.document().tracks.size());
}

QVariant TrackListModel::data(const QModelIndex& index, int role) const {
    const auto& tracks = project_.document().tracks;
    if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= tracks.size()) {
        return {};
    }
    const model::Track& track = tracks[static_cast<std::size_t>(index.row())];
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
        case Role::audioIn:
            return audioRoute(track.audioIn);
        case Role::audioOut:
            return audioRoute(track.audioOut);
        case Role::midiIn:
            return midiRoute(track.midiIn);
        case Role::midiOut:
            return midiRoute(track.midiOut);
        case Role::audioInText:
            return qs(routeText(track.audioIn));
        case Role::audioOutText:
            return qs(routeText(track.audioOut));
        case Role::midiInText:
            return qs(routeText(track.midiIn));
        case Role::midiOutText:
            return qs(routeText(track.midiOut));
        case Role::audioRoutes:
            return routes.audio;
        case Role::midiRoutes:
            return routes.midi;
        case Role::clips:
            return clipList(track);
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
                                                                          {Role::audioIn, "audioIn"},
                                                                          {Role::audioOut, "audioOut"},
                                                                          {Role::midiIn, "midiIn"},
                                                                          {Role::midiOut, "midiOut"},
                                                                          {Role::audioInText, "audioInText"},
                                                                          {Role::audioOutText, "audioOutText"},
                                                                          {Role::midiInText, "midiInText"},
                                                                          {Role::midiOutText, "midiOutText"},
                                                                          {Role::audioRoutes, "audioRoutes"},
                                                                          {Role::midiRoutes, "midiRoutes"},
                                                                          {Role::clips, "clips"}}};
    QHash<int, QByteArray> names;
    for (const auto& [role, name] : kNames) {
        names.insert(static_cast<int>(role), name);
    }
    return names;
}

void TrackListModel::changed(const model::Change& change) {
    const auto& tracks = project_.document().tracks;
    const auto found = change.track ? std::ranges::find(tracks, *change.track, &model::Track::id) : tracks.end();
    if (found == tracks.end()) {
        beginResetModel();
        endResetModel();
        return;
    }
    const QModelIndex row = index(static_cast<int>(found - tracks.begin()));
    emit dataChanged(row, row);
}

}  // namespace emerick::app
