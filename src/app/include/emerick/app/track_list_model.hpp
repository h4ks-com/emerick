#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVariant>
#include <cstdint>

#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/model/project.hpp"

namespace emerick::app {

class TrackListModel : public QAbstractListModel {
    Q_OBJECT

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
        audioIn,
        audioOut,
        midiIn,
        midiOut,
        audioInText,
        audioOutText,
        midiInText,
        midiOutText,
        audioRoutes,
        midiRoutes,
        clips,
        trackData,
        laneComponent,
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

private:
    void changed(const model::Change& change);

    [[nodiscard]] QVariantList clipList(const model::Track& track) const;

    ProjectController& controller_;
    model::Project& project_;
    const Registry& registry_;
    std::size_t listener_;
};

}  // namespace emerick::app
