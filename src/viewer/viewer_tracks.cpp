#include "viewer_tracks.hpp"

#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/track_params.hpp"
#include "emerick/app/track_rows.hpp"
#include "emerick/app/variant.hpp"
#include "emerick/model/automation.hpp"

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

}  // namespace

ViewerTracks::ViewerTracks(const ViewerProject& project, QObject* parent)
    : QAbstractListModel(parent), project_(project) {
    connect(&project_, &ViewerProject::projectChanged, this, [this] {
        beginResetModel();
        endResetModel();
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
        case Role::busRoutes:
            return false;
        case Role::gainDb:
            return track.gainDb;
        case Role::pan:
            return track.pan;
        case Role::instrumentId:
            return app::qs(track.instrument ? track.instrument->pluginId : std::string{});
        case Role::clips:
            return app::clipList(track, {.tempo = document.tempo, .sampleRate = kSampleRate, .folder = {}});
        case Role::trackData:
        case Role::takePreview:
            return QVariantMap{};
        case Role::laneComponent:
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
    static constexpr std::array<std::pair<Role, const char*>, 22> kNames{{{Role::id, "trackId"},
                                                                          {Role::name, "name"},
                                                                          {Role::icon, "icon"},
                                                                          {Role::colorIndex, "colorIndex"},
                                                                          {Role::mute, "mute"},
                                                                          {Role::solo, "solo"},
                                                                          {Role::armed, "armed"},
                                                                          {Role::monitor, "monitor"},
                                                                          {Role::gainDb, "gainDb"},
                                                                          {Role::pan, "pan"},
                                                                          {Role::busRoutes, "busRoutes"},
                                                                          {Role::instrumentId, "instrumentId"},
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

}  // namespace emerick::viewer
