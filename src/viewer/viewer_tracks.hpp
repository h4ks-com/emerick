#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVariant>
#include <cstdint>

#include "viewer_project.hpp"

namespace emerick::viewer {

// The rows the tracks view reads from the app's Tracks singleton, under the same role names, built by the app's own
// row builders from the document alone: no take previews, no live plugins and no track type lanes.
class ViewerTracks : public QAbstractListModel {
    Q_OBJECT

public:
    enum class Role : std::uint16_t {
        id = Qt::UserRole + 1,
        name,
        icon,
        colorIndex,
        mute,
        solo,
        armed,
        monitor,
        gainDb,
        pan,
        busRoutes,
        instrumentId,
        clips,
        trackData,
        laneComponent,
        headerComponent,
        fileDrop,
        clipType,
        automationMode,
        automation,
        automationParams,
        takePreview,
    };

    explicit ViewerTracks(const ViewerProject& project, QObject* parent = nullptr);

    [[nodiscard]] Q_INVOKABLE int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
    const ViewerProject& project_;
};

}  // namespace emerick::viewer
