#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <algorithm>
#include <nlohmann/json.hpp>

#include "emerick/app/enums.hpp"

namespace emerick::app {

inline constexpr double kMinPixelsPerTick = 0.002;
inline constexpr double kMaxPixelsPerTick = 0.8;
inline constexpr double kDefaultPixelsPerTick = 0.05;
inline constexpr int kMinTrackHeight = 36;
inline constexpr int kMaxTrackHeight = 160;
inline constexpr int kDefaultTrackHeight = 60;
inline constexpr int kMinKeyHeight = 6;
inline constexpr int kMaxKeyHeight = 32;
inline constexpr int kDefaultKeyHeight = 12;

// Zoom and selection change often, so each has its own signal and a zoom never re-evaluates selection bindings.
class Nav : public QObject {
    Q_OBJECT
    Q_PROPERTY(emerick::app::Page page READ page WRITE setPage NOTIFY changed)
    Q_PROPERTY(QString settingsTab READ settingsTab WRITE setSettingsTab NOTIFY changed)
    Q_PROPERTY(QString selectedTrack READ selectedTrack WRITE setSelectedTrack NOTIFY changed)
    Q_PROPERTY(QStringList selectedTracks READ selectedTracks WRITE setSelectedTracks NOTIFY changed)
    Q_PROPERTY(QString renamingTrack READ renamingTrack WRITE setRenamingTrack NOTIFY changed)
    Q_PROPERTY(QString selectedMarker READ selectedMarker WRITE setSelectedMarker NOTIFY selectionChanged)
    Q_PROPERTY(QString renamingMarker READ renamingMarker WRITE setRenamingMarker NOTIFY changed)
    Q_PROPERTY(QString renamingClip READ renamingClip WRITE setRenamingClip NOTIFY changed)
    Q_PROPERTY(QString trackMenu READ trackMenu WRITE setTrackMenu NOTIFY changed)
    Q_PROPERTY(bool paletteOpen READ paletteOpen WRITE setPaletteOpen NOTIFY changed)
    Q_PROPERTY(bool aboutOpen READ aboutOpen WRITE setAboutOpen NOTIFY changed)
    Q_PROPERTY(bool tunerOpen READ tunerOpen WRITE setTunerOpen NOTIFY changed)
    Q_PROPERTY(bool spectrogramOpen READ spectrogramOpen WRITE setSpectrogramOpen NOTIFY changed)
    Q_PROPERTY(bool healthOpen READ healthOpen WRITE setHealthOpen NOTIFY changed)
    Q_PROPERTY(bool librariesOpen READ librariesOpen WRITE setLibrariesOpen NOTIFY changed)
    Q_PROPERTY(QString shownLibrary READ shownLibrary WRITE setShownLibrary NOTIFY changed)
    Q_PROPERTY(bool inspectorOpen READ inspectorOpen WRITE setInspectorOpen NOTIFY changed)
    Q_PROPERTY(bool markerListOpen READ markerListOpen WRITE setMarkerListOpen NOTIFY changed)
    Q_PROPERTY(bool projectMenuOpen READ projectMenuOpen WRITE setProjectMenuOpen NOTIFY changed)
    Q_PROPERTY(bool consoleOpen READ consoleOpen WRITE setConsoleOpen NOTIFY changed)
    Q_PROPERTY(bool mixerOpen READ mixerOpen WRITE setMixerOpen NOTIFY changed)
    Q_PROPERTY(bool metronomeMenuOpen READ metronomeMenuOpen WRITE setMetronomeMenuOpen NOTIFY changed)
    Q_PROPERTY(bool pianoRollOpen READ pianoRollOpen WRITE setPianoRollOpen NOTIFY changed)
    Q_PROPERTY(QString pianoRollClip READ pianoRollClip WRITE setPianoRollClip NOTIFY changed)
    Q_PROPERTY(QString editingInsert READ editingInsert WRITE setEditingInsert NOTIFY changed)
    Q_PROPERTY(QVariantMap editingBlock READ editingBlock WRITE setEditingBlock NOTIFY changed)
    Q_PROPERTY(QStringList selectedNotes READ selectedNotes WRITE setSelectedNotes NOTIFY selectionChanged)
    Q_PROPERTY(int keyHeight READ keyHeight WRITE setKeyHeight NOTIFY viewChanged)
    Q_PROPERTY(bool automationOpen READ automationOpen WRITE setAutomationOpen NOTIFY changed)
    Q_PROPERTY(QVariantMap automationLanes READ automationLanes WRITE setAutomationLanes NOTIFY changed)
    Q_PROPERTY(QStringList selectedPoints READ selectedPoints WRITE setSelectedPoints NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedClips READ selectedClips WRITE setSelectedClips NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedBlocks READ selectedBlocks WRITE setSelectedBlocks NOTIFY selectionChanged)
    Q_PROPERTY(double pixelsPerTick READ pixelsPerTick WRITE setPixelsPerTick NOTIFY viewChanged)
    Q_PROPERTY(int trackHeight READ trackHeight WRITE setTrackHeight NOTIFY viewChanged)
    Q_PROPERTY(double scrollTicks READ scrollTicks WRITE setScrollTicks NOTIFY scrollChanged)
    Q_PROPERTY(double laneWidth READ laneWidth WRITE setLaneWidth NOTIFY scrollChanged)

public:
    using QObject::QObject;

    [[nodiscard]] Page page() const { return page_; }
    [[nodiscard]] QString settingsTab() const { return settingsTab_; }
    // The track the inspector shows, which is always one of the selected tracks.
    [[nodiscard]] QString selectedTrack() const { return selectedTrack_; }
    [[nodiscard]] QStringList selectedTracks() const { return selectedTracks_; }
    // The track whose header shows its name field.
    [[nodiscard]] QString renamingTrack() const { return renamingTrack_; }
    // Selecting clips deselects the marker and selecting a marker deselects the clips, so Delete removes what was
    // picked last.
    [[nodiscard]] QString selectedMarker() const { return selectedMarker_; }
    // The marker whose flag shows its name field.
    [[nodiscard]] QString renamingMarker() const { return renamingMarker_; }
    [[nodiscard]] QString renamingClip() const { return renamingClip_; }
    // The track whose context menu is open.
    [[nodiscard]] QString trackMenu() const { return trackMenu_; }
    [[nodiscard]] bool paletteOpen() const { return paletteOpen_; }
    [[nodiscard]] bool aboutOpen() const { return aboutOpen_; }
    [[nodiscard]] bool tunerOpen() const { return tunerOpen_; }
    [[nodiscard]] bool spectrogramOpen() const { return spectrogramOpen_; }
    [[nodiscard]] bool healthOpen() const { return healthOpen_; }
    [[nodiscard]] bool librariesOpen() const { return librariesOpen_; }
    // The library the libraries dialog scrolls to and marks, or empty.
    [[nodiscard]] QString shownLibrary() const { return shownLibrary_; }
    [[nodiscard]] bool inspectorOpen() const { return inspectorOpen_; }
    // Whether the inspector lists the markers.
    [[nodiscard]] bool markerListOpen() const { return markerListOpen_; }
    [[nodiscard]] bool projectMenuOpen() const { return projectMenuOpen_; }
    [[nodiscard]] bool consoleOpen() const { return consoleOpen_; }
    [[nodiscard]] bool mixerOpen() const { return mixerOpen_; }
    [[nodiscard]] bool metronomeMenuOpen() const { return metronomeMenuOpen_; }
    [[nodiscard]] bool pianoRollOpen() const { return pianoRollOpen_; }
    // The MIDI clip the piano roll edits, and the ids of its selected notes.
    [[nodiscard]] QString pianoRollClip() const { return pianoRollClip_; }
    [[nodiscard]] QStringList selectedNotes() const { return selectedNotes_; }
    // The insert whose effect editor is open.
    [[nodiscard]] QString editingInsert() const { return editingInsert_; }
    // The lane block whose picker is open, as its track and its position in ticks, or empty.
    [[nodiscard]] QVariantMap editingBlock() const { return editingBlock_; }
    // The height of one key row in the piano roll, its vertical zoom.
    [[nodiscard]] int keyHeight() const { return keyHeight_; }
    // Whether tracks show their automation lanes, and per track id the parameter its lane shows, gain by default.
    [[nodiscard]] bool automationOpen() const { return automationOpen_; }
    [[nodiscard]] QVariantMap automationLanes() const { return automationLanes_; }
    [[nodiscard]] QString automationLane(const QString& trackId) const {
        return automationLanes_.value(trackId, QStringLiteral("gain")).toString();
    }
    // Selected automation points, in any lane; selecting them deselects the clips and the marker.
    [[nodiscard]] QStringList selectedPoints() const { return selectedPoints_; }
    [[nodiscard]] QStringList selectedClips() const { return selectedClips_; }
    // The selected lane blocks as their track and the tick range from to to they cover, or empty.
    [[nodiscard]] QVariantMap selectedBlocks() const { return selectedBlocks_; }
    [[nodiscard]] double pixelsPerTick() const { return pixelsPerTick_; }
    [[nodiscard]] int trackHeight() const { return trackHeight_; }
    // The tick at the timeline's left edge, and the lanes' width in pixels, which the timeline view reports.
    [[nodiscard]] double scrollTicks() const { return scrollTicks_; }
    [[nodiscard]] double laneWidth() const { return laneWidth_; }

    void setPage(Page value) { assign(page_, value, &Nav::changed); }
    void setSettingsTab(const QString& value) { assign(settingsTab_, value, &Nav::changed); }
    void setSelectedTrack(const QString& value) {
        selectTracks(value.isEmpty() ? QStringList{} : QStringList{value}, value);
    }
    void setSelectedTracks(const QStringList& value) {
        selectTracks(value, value.contains(selectedTrack_) || value.isEmpty() ? selectedTrack_ : value.back());
    }
    void selectTracks(const QStringList& tracks, const QString& primary) {
        assign(selectedTracks_, tracks, &Nav::changed);
        assign(selectedTrack_, tracks.contains(primary) ? primary : QString{}, &Nav::changed);
    }
    void setRenamingTrack(const QString& value) { assign(renamingTrack_, value, &Nav::changed); }
    void setSelectedMarker(const QString& value) {
        if (!value.isEmpty()) {
            assign(selectedClips_, {}, &Nav::selectionChanged);
            assign(selectedPoints_, {}, &Nav::selectionChanged);
            assign(selectedBlocks_, {}, &Nav::selectionChanged);
        }
        assign(selectedMarker_, value, &Nav::selectionChanged);
    }
    void setRenamingMarker(const QString& value) { assign(renamingMarker_, value, &Nav::changed); }
    void setRenamingClip(const QString& value) { assign(renamingClip_, value, &Nav::changed); }
    void setTrackMenu(const QString& value) { assign(trackMenu_, value, &Nav::changed); }
    void setPaletteOpen(bool value) { assign(paletteOpen_, value, &Nav::changed); }
    void setAboutOpen(bool value) { assign(aboutOpen_, value, &Nav::changed); }
    void setTunerOpen(bool value) { assign(tunerOpen_, value, &Nav::changed); }
    void setSpectrogramOpen(bool value) { assign(spectrogramOpen_, value, &Nav::changed); }
    void setHealthOpen(bool value) { assign(healthOpen_, value, &Nav::changed); }
    void setLibrariesOpen(bool value) { assign(librariesOpen_, value, &Nav::changed); }
    void setShownLibrary(const QString& value) { assign(shownLibrary_, value, &Nav::changed); }
    void setInspectorOpen(bool value) { assign(inspectorOpen_, value, &Nav::changed); }
    void setMarkerListOpen(bool value) { assign(markerListOpen_, value, &Nav::changed); }
    void setProjectMenuOpen(bool value) { assign(projectMenuOpen_, value, &Nav::changed); }
    void setConsoleOpen(bool value) { assign(consoleOpen_, value, &Nav::changed); }
    void setMixerOpen(bool value) { assign(mixerOpen_, value, &Nav::changed); }
    void setMetronomeMenuOpen(bool value) { assign(metronomeMenuOpen_, value, &Nav::changed); }
    void setPianoRollOpen(bool value) { assign(pianoRollOpen_, value, &Nav::changed); }
    void setPianoRollClip(const QString& value) {
        if (value != pianoRollClip_) {
            assign(selectedNotes_, {}, &Nav::selectionChanged);
        }
        assign(pianoRollClip_, value, &Nav::changed);
    }
    void setEditingInsert(const QString& value) { assign(editingInsert_, value, &Nav::changed); }
    void setEditingBlock(const QVariantMap& value) { assign(editingBlock_, value, &Nav::changed); }
    void setSelectedNotes(const QStringList& value) { assign(selectedNotes_, value, &Nav::selectionChanged); }
    void setKeyHeight(int value) {
        assign(keyHeight_, std::clamp(value, kMinKeyHeight, kMaxKeyHeight), &Nav::viewChanged);
    }
    void setAutomationOpen(bool value) { assign(automationOpen_, value, &Nav::changed); }
    void setAutomationLanes(const QVariantMap& value) { assign(automationLanes_, value, &Nav::changed); }
    void setSelectedPoints(const QStringList& value) {
        if (!value.isEmpty()) {
            assign(selectedClips_, {}, &Nav::selectionChanged);
            assign(selectedMarker_, {}, &Nav::selectionChanged);
            assign(selectedBlocks_, {}, &Nav::selectionChanged);
        }
        assign(selectedPoints_, value, &Nav::selectionChanged);
    }
    void setSelectedClips(const QStringList& value) {
        if (!value.isEmpty()) {
            assign(selectedMarker_, {}, &Nav::selectionChanged);
            assign(selectedPoints_, {}, &Nav::selectionChanged);
            assign(selectedBlocks_, {}, &Nav::selectionChanged);
        }
        assign(selectedClips_, value, &Nav::selectionChanged);
    }
    void setSelectedBlocks(const QVariantMap& value) {
        if (!value.isEmpty()) {
            assign(selectedClips_, {}, &Nav::selectionChanged);
            assign(selectedMarker_, {}, &Nav::selectionChanged);
            assign(selectedPoints_, {}, &Nav::selectionChanged);
        }
        assign(selectedBlocks_, value, &Nav::selectionChanged);
    }
    void setPixelsPerTick(double value) {
        assign(pixelsPerTick_, std::clamp(value, kMinPixelsPerTick, kMaxPixelsPerTick), &Nav::viewChanged);
    }
    void setTrackHeight(int value) {
        assign(trackHeight_, std::clamp(value, kMinTrackHeight, kMaxTrackHeight), &Nav::viewChanged);
    }
    void setScrollTicks(double value) { assign(scrollTicks_, std::max(0.0, value), &Nav::scrollChanged); }
    void zoomAround(double factor, double anchorTicks) {
        const double x = (anchorTicks - scrollTicks_) * pixelsPerTick_;
        setPixelsPerTick(pixelsPerTick_ * factor);
        setScrollTicks(anchorTicks - x / pixelsPerTick_);
    }
    void setLaneWidth(double value) { assign(laneWidth_, value, &Nav::scrollChanged); }

    [[nodiscard]] nlohmann::json toJson() const {
        const auto strings = [](const QStringList& values) {
            nlohmann::json list = nlohmann::json::array();
            for (const QString& value : values) {
                list.push_back(value.toStdString());
            }
            return list;
        };
        return {
            {"page", enumKey(page_)},
            {"settingsTab", settingsTab_.toStdString()},
            {"selectedTrack", selectedTrack_.toStdString()},
            {"selectedTracks", strings(selectedTracks_)},
            {"selectedClips", strings(selectedClips_)},
            {"selectedMarker", selectedMarker_.toStdString()},
            {"pixelsPerTick", pixelsPerTick_},
            {"trackHeight", trackHeight_},
            {"scrollTicks", scrollTicks_},
            {"paletteOpen", paletteOpen_},
            {"aboutOpen", aboutOpen_},
            {"tunerOpen", tunerOpen_},
            {"spectrogramOpen", spectrogramOpen_},
            {"healthOpen", healthOpen_},
            {"librariesOpen", librariesOpen_},
            {"shownLibrary", shownLibrary_.toStdString()},
            {"inspectorOpen", inspectorOpen_},
            {"markerListOpen", markerListOpen_},
            {"projectMenuOpen", projectMenuOpen_},
            {"consoleOpen", consoleOpen_},
            {"mixerOpen", mixerOpen_},
            {"metronomeMenuOpen", metronomeMenuOpen_},
            {"pianoRollOpen", pianoRollOpen_},
            {"pianoRollClip", pianoRollClip_.toStdString()},
            {"selectedNotes", strings(selectedNotes_)},
            {"editingInsert", editingInsert_.toStdString()},
            {"editingBlock", editingBlock_.isEmpty()
                                 ? nlohmann::json::object()
                                 : nlohmann::json{{"track", editingBlock_.value("track").toString().toStdString()},
                                                  {"at", editingBlock_.value("at").toLongLong()}}},
            {"selectedBlocks", selectedBlocks_.isEmpty()
                                   ? nlohmann::json::object()
                                   : nlohmann::json{{"track", selectedBlocks_.value("track").toString().toStdString()},
                                                    {"from", selectedBlocks_.value("from").toLongLong()},
                                                    {"to", selectedBlocks_.value("to").toLongLong()}}},
            {"keyHeight", keyHeight_},
            {"automationOpen", automationOpen_},
            {"automationLanes", lanes()},
            {"selectedPoints", strings(selectedPoints_)}};
    }

signals:
    void changed();
    void selectionChanged();
    void viewChanged();
    void scrollChanged();

private:
    [[nodiscard]] nlohmann::json lanes() const {
        nlohmann::json lanes = nlohmann::json::object();
        for (auto entry = automationLanes_.cbegin(); entry != automationLanes_.cend(); ++entry) {
            lanes[entry.key().toStdString()] = entry.value().toString().toStdString();
        }
        return lanes;
    }

    template <class T>
    void assign(T& field, const T& value, void (Nav::*signal)()) {
        if (field != value) {
            field = value;
            emit(this->*signal)();
        }
    }

    Page page_ = Page::Main;
    QString settingsTab_ = QStringLiteral("general");
    QString selectedTrack_;
    QStringList selectedTracks_;
    QString renamingTrack_;
    QString selectedMarker_;
    QString renamingMarker_;
    QString renamingClip_;
    QString trackMenu_;
    bool paletteOpen_ = false;
    bool aboutOpen_ = false;
    bool tunerOpen_ = false;
    bool spectrogramOpen_ = false;
    bool healthOpen_ = false;
    bool librariesOpen_ = false;
    QString shownLibrary_;
    bool inspectorOpen_ = true;
    bool markerListOpen_ = true;
    bool projectMenuOpen_ = false;
    bool consoleOpen_ = false;
    bool mixerOpen_ = false;
    bool metronomeMenuOpen_ = false;
    bool pianoRollOpen_ = false;
    QString pianoRollClip_;
    QStringList selectedNotes_;
    QString editingInsert_;
    QVariantMap editingBlock_;
    int keyHeight_ = kDefaultKeyHeight;
    bool automationOpen_ = false;
    QVariantMap automationLanes_;
    QStringList selectedPoints_;
    QStringList selectedClips_;
    QVariantMap selectedBlocks_;
    double pixelsPerTick_ = kDefaultPixelsPerTick;
    int trackHeight_ = kDefaultTrackHeight;
    double scrollTicks_ = 0;
    double laneWidth_ = 0;
};

}  // namespace emerick::app
