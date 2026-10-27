#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVariant>
#include <QVariantList>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/project_actions.hpp"
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
        playsAudio,
        instrumentId,
        instrumentName,
        sends,
        inserts,
        outputBusName,
        clips,
        trackData,
        laneComponent,
        headerComponent,
        clipType,
        automationMode,
        automation,
        automationParams,
        recordDelayMs,
        takePreview,
        inspectorComponent,
        fileDrop,
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
    // The MIDI clip's points on a controller lane, flat as at, value, at, value, or empty for any other lane.
    Q_INVOKABLE QList<double> controlPoints(const QString& clip, const QString& lane) const;
    // The controller lanes the piano roll offers for the MIDI clip as {id, title, hasData}.
    Q_INVOKABLE QVariantList laneChoices(const QString& clip) const;
    // The insert as project state lists it, with its track's id and name, or an empty map when it is gone.
    Q_INVOKABLE QVariantMap insert(const QString& id) const;
    // Whether every listed track has the same value of the named role, so a view can show "Mixed" where they differ.
    Q_INVOKABLE bool sameAcross(const QStringList& ids, const QString& role) const;

signals:
    void revisionChanged();

private:
    void changed(const model::Change& change);
    // Rows whose controls follow automation update as the playhead moves.
    void transportMoved();
    void takePreviewChanged(const QString& trackId, bool trackDataChanged);
    // Removes, moves and inserts rows until they match the document's tracks, so views keep their other delegates.
    void syncRows();

    [[nodiscard]] const model::MidiClip* midiClipWithId(const QString& id) const;
    [[nodiscard]] QVariantMap previewEntry(const model::TrackId& id, ClipType type) const;
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

    // The notes of a take's frozen chunks, as previewEntry lists them, and the lowest and highest key among them.
    struct FrozenNotes {
        std::uint64_t take = 0;
        std::size_t clips = 0;
        QVariantList segments;
        std::pair<int, int> keys{std::numeric_limits<int>::max(), std::numeric_limits<int>::min()};
    };
    mutable std::map<std::string, FrozenNotes> frozenNotes_;
};

}  // namespace emerick::app
