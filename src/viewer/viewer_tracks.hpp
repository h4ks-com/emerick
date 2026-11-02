#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>

#include "viewer_project.hpp"

namespace emerick::viewer {

// The rows the tracks view and the mixer read from the app's Tracks singleton, under the same role names, built by
// the app's own row builders from the document alone: no take previews, no live plugins and no track type lanes.
class ViewerTracks : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

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
        trimDb,
        pan,
        audioOut,
        midiOut,
        audioRoutes,
        midiRoutes,
        busRoutes,
        playsAudio,
        instrumentId,
        instrumentName,
        instrument,
        sends,
        inserts,
        outputBusName,
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

    [[nodiscard]] int revision() const { return revision_; }
    // The clip as a track row lists it, with its track's id, name and color, or an empty map when it is gone.
    Q_INVOKABLE QVariantMap clip(const QString& id) const;
    Q_INVOKABLE QList<double> controlPoints(const QString& clip, const QString& lane) const;
    Q_INVOKABLE QVariantList laneChoices(const QString& clip) const;

signals:
    void revisionChanged();

private:
    [[nodiscard]] const model::MidiClip* midiClip(const QString& id) const;

    const ViewerProject& project_;
    int revision_ = 0;
};

}  // namespace emerick::viewer
