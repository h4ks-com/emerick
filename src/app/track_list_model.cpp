#include "emerick/app/track_list_model.hpp"

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <ranges>
#include <span>
#include <utility>
#include <variant>

#include "emerick/app/automation_params.hpp"
#include "emerick/app/note_edit.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/track_rows.hpp"
#include "emerick/model/automation.hpp"
#include "emerick/model/controls.hpp"
#include "emerick/model/midi_notes.hpp"

namespace emerick::app {

namespace {

// QML compares this with a clip's type.
const char* clipTypeName(ClipType type) {
    constexpr std::array kNames{"", "audio", "midi"};
    return kNames.at(static_cast<std::size_t>(type));
}

constexpr std::size_t kChunksPerSegment = 8;

const model::MidiClip* midiClip(const std::vector<model::Clip>& clips, std::size_t index) {
    return index < clips.size() ? std::get_if<model::MidiClip>(&clips[index]) : nullptr;
}

void widenKeys(std::pair<int, int>& keys, const model::MidiClip& clip) {
    for (const model::Note& note : clip.notes) {
        keys = {std::min(keys.first, note.pitch), std::max(keys.second, note.pitch)};
    }
}

}  // namespace

// The take in progress on a clip lane, as the tracks area draws it, or an empty map. We keep the frozen chunks' notes
// in segments of a few chunks, so an update converts only the live chunk's notes and a freeze repaints one segment.
QVariantMap TrackListModel::previewEntry(const model::TrackId& id, ClipType type) const {
    const TakePreview* preview = controller_.takePreview(id);
    if (preview == nullptr || type == ClipType::none) {
        return {};
    }
    const core::Ticks liveFrom = preview->live.startTick - preview->start;
    FrozenNotes& frozen = frozenNotes_[id.value];
    if (frozen.take != preview->take) {
        frozen = FrozenNotes{};
        frozen.take = preview->take;
    }
    if (preview->frozen) {
        const std::vector<model::Clip>& clips = preview->frozen->clips;
        for (; frozen.clips < clips.size(); ++frozen.clips) {
            const model::MidiClip* midi = midiClip(clips, frozen.clips);
            if (midi == nullptr) {
                continue;
            }
            const core::Ticks at = midi->start - preview->start;
            if (frozen.clips % kChunksPerSegment == 0) {
                frozen.segments.append(QVariantMap{{"start", QVariant::fromValue(at)}, {"notes", QVariantList{}}});
            }
            QVariantMap segment = frozen.segments.last().toMap();
            const core::Ticks from = segment["start"].toLongLong();
            QVariantList notes = segment["notes"].toList();
            notes.append(noteList(*midi, at - from));
            segment["notes"] = notes;
            segment["length"] = QVariant::fromValue(at + midi->length - from);
            frozen.segments.last() = segment;
            widenKeys(frozen.keys, *midi);
        }
    }
    std::pair<int, int> keys = frozen.keys;
    QVariantMap entry{{"type", clipTypeName(type)},
                      {"start", QVariant::fromValue(preview->start)},
                      {"length", QVariant::fromValue(preview->length)},
                      {"file", qs(preview->audioFile.string())},
                      {"frames", QVariant::fromValue(preview->frames)},
                      {"frozen", frozen.segments},
                      {"liveFrom", QVariant::fromValue(liveFrom)},
                      {"notes", QVariantList{}}};
    if (const model::MidiClip* live = preview->track ? midiClip(preview->track->clips, 0) : nullptr) {
        entry["notes"] = noteList(*live);
        entry["length"] = QVariant::fromValue(std::max(preview->length, liveFrom + live->length));
        widenKeys(keys, *live);
    }
    entry["lowKey"] = keys.first <= keys.second ? keys.first : -1;
    entry["highKey"] = keys.first <= keys.second ? keys.second : -1;
    return entry;
}

RowContext TrackListModel::rowContext() const {
    return {.tempo = project_.document().tempo, .sampleRate = controller_.sampleRate(), .folder = controller_.folder()};
}

QVariantMap TrackListModel::clip(const QString& id) const {
    const auto found = project_.findClip(model::ClipId{id.toStdString()});
    if (!found) {
        return {};
    }
    const auto& clips = found->track->clips;
    const auto take = std::ranges::find(clips, found->clip, [](const model::Clip& clip) { return &clip; });
    QVariantMap entry = clipEntry(*found->clip, static_cast<int>(take - clips.begin()) + 1, rowContext());
    entry.insert({{"trackId", qs(found->track->id.value)},
                  {"trackName", qs(found->track->name)},
                  {"colorIndex", found->track->color}});
    return entry;
}

const model::MidiClip* TrackListModel::midiClipWithId(const QString& id) const {
    const auto found = project_.findClip(model::ClipId{id.toStdString()});
    return found ? std::get_if<model::MidiClip>(found->clip) : nullptr;
}

QList<double> TrackListModel::controlPoints(const QString& clip, const QString& lane) const {
    const model::MidiClip* midi = midiClipWithId(clip);
    return midi == nullptr ? QList<double>{} : controlPointList(*midi, lane);
}

QVariantList TrackListModel::laneChoices(const QString& clip) const {
    const model::MidiClip* midi = midiClipWithId(clip);
    return midi == nullptr ? QVariantList{} : toVariant(app::laneChoices(*midi)).toList();
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

namespace {

// A plugin's params in the shape the effect panel reads for a built-in effect, each going by its name.
Json pluginParamList(const engine::Plugin& plugin) {
    return plugin.params() | std::views::transform([](const engine::PluginParam& param) {
               const char* kind = param.stepped ? "choice" : "number";
               return Json{{"id", param.name},     {"title", param.name},  {"unit", ""},
                           {"minimum", param.min}, {"maximum", param.max}, {"defaultValue", param.defaultValue},
                           {"kind", kind},         {"logarithmic", false}, {"choices", param.choices}};
           }) |
           std::ranges::to<Json::array_t>();
}

}  // namespace

QVariantMap TrackListModel::insert(const QString& id) const {
    const model::InsertId insertId{id.toStdString()};
    const model::Track* track = model::trackWithInsert(project_.document(), insertId);
    if (track == nullptr) {
        return {};
    }
    Json state = insertState(*std::ranges::find(track->inserts, insertId, &model::Insert::id));
    state["track"] = track->id.value;
    state["trackName"] = track->name;
    if (const auto plugin = controller_.instruments().find(insertId)) {
        state["pluginParams"] = pluginParamList(*plugin);
        for (const engine::PluginParam& param : plugin->params()) {
            state["params"][param.name] = plugin->paramValue(param.id).value_or(param.defaultValue);
        }
    }
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
    connect(&controller_, &ProjectController::takePreviewChanged, this, &TrackListModel::takePreviewChanged);
}

void TrackListModel::takePreviewChanged(const QString& trackId, bool trackDataChanged) {
    const model::TrackId id{trackId.toStdString()};
    if (controller_.takePreview(id) == nullptr) {
        frozenNotes_.erase(id.value);
    }
    const auto row = std::ranges::find(rows_, id);
    if (row == rows_.end()) {
        return;
    }
    const QModelIndex at = index(static_cast<int>(row - rows_.begin()));
    if (trackDataChanged) {
        emit dataChanged(at, at, {static_cast<int>(Role::takePreview), static_cast<int>(Role::trackData)});
    } else {
        emit dataChanged(at, at, {static_cast<int>(Role::takePreview)});
    }
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
        case Role::trimDb:
            return track.trimDb;
        case Role::pan:
            return controller_.automatedValue(track, "pan").value_or(track.pan);
        case Role::audioIn:
            return audioRouteEntry(track.audioIn);
        case Role::audioOut:
            return audioRouteEntry(track.audioOut);
        case Role::midiIn:
            return midiRouteEntry(track.midiIn);
        case Role::midiOut:
            return midiRouteEntry(track.midiOut);
        case Role::audioRoutes:
            return routes.audio;
        case Role::midiRoutes:
            return routes.midi;
        case Role::busRoutes:
            return routes.bus;
        case Role::playsAudio:
            return type != nullptr && app::playsAudio(*type, track);
        case Role::instrumentId:
            return qs(track.instrument ? track.instrument->pluginId : std::string{});
        case Role::instrumentName:
            return qs(track.instrument ? track.instrument->name : std::string{});
        case Role::instrument:
            return instrumentEntry(track);
        case Role::sends:
            return sendList(track, project_.document(), [&](const model::Send& send) {
                return controller_.automatedValue(track, sendParamId(send.bus)).value_or(send.levelDb);
            });
        case Role::inserts:
            return insertList(track);
        case Role::outputBusName: {
            const model::Track* bus = project_.track(track.outputBus);
            return bus != nullptr ? qs(bus->name) : QString{};
        }
        case Role::clips:
            return clipList(track, rowContext());
        case Role::trackData: {
            const TakePreview* preview = controller_.takePreview(track.id);
            return toVariant(preview != nullptr && preview->track ? preview->track->data : track.data);
        }
        case Role::laneComponent:
            return qs(type != nullptr ? type->laneComponent : std::string{});
        case Role::headerComponent:
            return qs(type != nullptr ? type->headerComponent : std::string{});
        case Role::clipType:
            return qs(clipTypeName(clipTypeOf(type)));
        case Role::automationMode:
            return qs(std::string{model::automationModeId(track.automationMode)});
        case Role::automation:
            return laneList(track);
        case Role::automationParams:
            return paramList(
                automationParams(registry_, project_.document(), track, controller_.instruments().insertPlugins()));
        case Role::recordDelayMs:
            return track.recordDelayMs;
        case Role::takePreview:
            return previewEntry(track.id, clipTypeOf(type));
        case Role::inspectorComponent:
            return qs(type != nullptr ? type->inspectorComponent : std::string{});
        case Role::fileDrop:
            return type != nullptr && !type->fileDrop.action.empty()
                       ? QVariantMap{{"action", qs(type->fileDrop.action)},
                                     {"extensions", toVariant(Json(type->fileDrop.extensions))}}
                       : QVariant{};
    }
    return {};
}

QHash<int, QByteArray> TrackListModel::roleNames() const {
    static constexpr std::array<std::pair<Role, const char*>, 38> kNames{
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
         {Role::trimDb, "trimDb"},
         {Role::pan, "pan"},
         {Role::audioIn, "audioIn"},
         {Role::audioOut, "audioOut"},
         {Role::midiIn, "midiIn"},
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
         {Role::clipType, "clipType"},
         {Role::automationMode, "automationMode"},
         {Role::automation, "automation"},
         {Role::automationParams, "automationParams"},
         {Role::recordDelayMs, "recordDelayMs"},
         {Role::takePreview, "takePreview"},
         {Role::inspectorComponent, "inspectorComponent"},
         {Role::fileDrop, "fileDrop"}}};
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
