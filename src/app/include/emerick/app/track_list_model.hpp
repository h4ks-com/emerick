#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVariant>
#include <cstdint>
#include <vector>

#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/model/project.hpp"

namespace emerick::app {

class TrackListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    enum class Role : std::uint16_t {
        id = Qt::UserRole + 1,
        name,
        kind,
        icon,
        colorIndex,
        mute,
        solo,
        armed,
        monitor,
        gainDb,
        pan,
        audioIn,
        audioOut,
        midiIn,
        midiOut,
        audioRoutes,
        midiRoutes,
        busRoutes,
        sends,
        outputBusName,
        clips,
        trackData,
        laneComponent,
        clipType,
        automationMode,
        automation,
        automationParams,
    };

    TrackListModel(ProjectController& controller, const Registry& registry, QObject* parent = nullptr);
    ~TrackListModel() override;
    TrackListModel(const TrackListModel&) = delete;
    TrackListModel& operator=(const TrackListModel&) = delete;
    TrackListModel(TrackListModel&&) = delete;
    TrackListModel& operator=(TrackListModel&&) = delete;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Grows with every project change, so a binding that reads it with clip() follows the clip.
    [[nodiscard]] int revision() const { return revision_; }
    // The clip as a track row lists it, with its track's id, name and color, or an empty map when it is gone.
    Q_INVOKABLE QVariantMap clip(const QString& id) const;

signals:
    void revisionChanged();

private:
    void changed(const model::Change& change);
    // Rows whose controls follow automation update as the playhead moves.
    void transportMoved();
    // Removes, moves and inserts rows until they match the document's tracks, so views keep their other delegates.
    void syncRows();

    [[nodiscard]] QVariantMap clipEntry(const model::Clip& clip, int take) const;
    [[nodiscard]] QVariantList clipList(const model::Track& track) const;
    [[nodiscard]] QVariantList sendList(const model::Track& track) const;
    [[nodiscard]] QVariantList paramList(const model::Track& track) const;
    [[nodiscard]] QString trackName(const model::TrackId& id) const;
    [[nodiscard]] bool isBus(const model::TrackId& id) const;

    ProjectController& controller_;
    model::Project& project_;
    const Registry& registry_;
    // The track ids the view holds rows for; data reads through them, so the rows stay right while we sync.
    std::vector<model::TrackId> rows_;
    std::size_t listener_;
    int revision_ = 0;
};

}  // namespace emerick::app
