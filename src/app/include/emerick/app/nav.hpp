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
    Q_PROPERTY(QString renamingTrack READ renamingTrack WRITE setRenamingTrack NOTIFY changed)
    Q_PROPERTY(QString selectedMarker READ selectedMarker WRITE setSelectedMarker NOTIFY selectionChanged)
    Q_PROPERTY(QString renamingMarker READ renamingMarker WRITE setRenamingMarker NOTIFY changed)
    Q_PROPERTY(QString trackMenu READ trackMenu WRITE setTrackMenu NOTIFY changed)
    Q_PROPERTY(bool paletteOpen READ paletteOpen WRITE setPaletteOpen NOTIFY changed)
    Q_PROPERTY(bool aboutOpen READ aboutOpen WRITE setAboutOpen NOTIFY changed)
    Q_PROPERTY(bool inspectorOpen READ inspectorOpen WRITE setInspectorOpen NOTIFY changed)
    Q_PROPERTY(bool projectMenuOpen READ projectMenuOpen WRITE setProjectMenuOpen NOTIFY changed)
    Q_PROPERTY(bool consoleOpen READ consoleOpen WRITE setConsoleOpen NOTIFY changed)
    Q_PROPERTY(bool mixerOpen READ mixerOpen WRITE setMixerOpen NOTIFY changed)
    Q_PROPERTY(bool metronomeMenuOpen READ metronomeMenuOpen WRITE setMetronomeMenuOpen NOTIFY changed)
    Q_PROPERTY(bool pianoRollOpen READ pianoRollOpen WRITE setPianoRollOpen NOTIFY changed)
    Q_PROPERTY(QString pianoRollClip READ pianoRollClip WRITE setPianoRollClip NOTIFY changed)
    Q_PROPERTY(QStringList selectedNotes READ selectedNotes WRITE setSelectedNotes NOTIFY selectionChanged)
    Q_PROPERTY(int keyHeight READ keyHeight WRITE setKeyHeight NOTIFY viewChanged)
    Q_PROPERTY(bool automationOpen READ automationOpen WRITE setAutomationOpen NOTIFY changed)
    Q_PROPERTY(QVariantMap automationLanes READ automationLanes WRITE setAutomationLanes NOTIFY changed)
    Q_PROPERTY(QStringList selectedPoints READ selectedPoints WRITE setSelectedPoints NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedClips READ selectedClips WRITE setSelectedClips NOTIFY selectionChanged)
    Q_PROPERTY(double pixelsPerTick READ pixelsPerTick WRITE setPixelsPerTick NOTIFY viewChanged)
    Q_PROPERTY(int trackHeight READ trackHeight WRITE setTrackHeight NOTIFY viewChanged)
    Q_PROPERTY(double scrollTicks READ scrollTicks WRITE setScrollTicks NOTIFY scrollChanged)
    Q_PROPERTY(double laneWidth READ laneWidth WRITE setLaneWidth NOTIFY scrollChanged)

public:
    using QObject::QObject;

    [[nodiscard]] Page page() const { return page_; }
    [[nodiscard]] QString settingsTab() const { return settingsTab_; }
    [[nodiscard]] QString selectedTrack() const { return selectedTrack_; }
    // The track whose header shows its name field.
    [[nodiscard]] QString renamingTrack() const { return renamingTrack_; }
    // Selecting clips deselects the marker and selecting a marker deselects the clips, so Delete removes what was
    // picked last.
    [[nodiscard]] QString selectedMarker() const { return selectedMarker_; }
    // The marker whose flag shows its name field.
    [[nodiscard]] QString renamingMarker() const { return renamingMarker_; }
    // The track whose context menu is open.
    [[nodiscard]] QString trackMenu() const { return trackMenu_; }
    [[nodiscard]] bool paletteOpen() const { return paletteOpen_; }
    [[nodiscard]] bool aboutOpen() const { return aboutOpen_; }
    [[nodiscard]] bool inspectorOpen() const { return inspectorOpen_; }
    [[nodiscard]] bool projectMenuOpen() const { return projectMenuOpen_; }
    [[nodiscard]] bool consoleOpen() const { return consoleOpen_; }
    [[nodiscard]] bool mixerOpen() const { return mixerOpen_; }
    [[nodiscard]] bool metronomeMenuOpen() const { return metronomeMenuOpen_; }
    [[nodiscard]] bool pianoRollOpen() const { return pianoRollOpen_; }
    // The MIDI clip the piano roll edits, and the ids of its selected notes.
    [[nodiscard]] QString pianoRollClip() const { return pianoRollClip_; }
    [[nodiscard]] QStringList selectedNotes() const { return selectedNotes_; }
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
    [[nodiscard]] double pixelsPerTick() const { return pixelsPerTick_; }
    [[nodiscard]] int trackHeight() const { return trackHeight_; }
    // The tick at the timeline's left edge, and the lanes' width in pixels, which the timeline view reports.
    [[nodiscard]] double scrollTicks() const { return scrollTicks_; }
    [[nodiscard]] double laneWidth() const { return laneWidth_; }

    void setPage(Page value) { assign(page_, value, &Nav::changed); }
    void setSettingsTab(const QString& value) { assign(settingsTab_, value, &Nav::changed); }
    void setSelectedTrack(const QString& value) { assign(selectedTrack_, value, &Nav::changed); }
    void setRenamingTrack(const QString& value) { assign(renamingTrack_, value, &Nav::changed); }
    void setSelectedMarker(const QString& value) {
        if (!value.isEmpty()) {
            assign(selectedClips_, {}, &Nav::selectionChanged);
            assign(selectedPoints_, {}, &Nav::selectionChanged);
        }
        assign(selectedMarker_, value, &Nav::selectionChanged);
    }
    void setRenamingMarker(const QString& value) { assign(renamingMarker_, value, &Nav::changed); }
    void setTrackMenu(const QString& value) { assign(trackMenu_, value, &Nav::changed); }
    void setPaletteOpen(bool value) { assign(paletteOpen_, value, &Nav::changed); }
    void setAboutOpen(bool value) { assign(aboutOpen_, value, &Nav::changed); }
    void setInspectorOpen(bool value) { assign(inspectorOpen_, value, &Nav::changed); }
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
        }
        assign(selectedPoints_, value, &Nav::selectionChanged);
    }
    void setSelectedClips(const QStringList& value) {
        if (!value.isEmpty()) {
            assign(selectedMarker_, {}, &Nav::selectionChanged);
            assign(selectedPoints_, {}, &Nav::selectionChanged);
        }
        assign(selectedClips_, value, &Nav::selectionChanged);
    }
    void setPixelsPerTick(double value) {
        assign(pixelsPerTick_, std::clamp(value, kMinPixelsPerTick, kMaxPixelsPerTick), &Nav::viewChanged);
    }
    void setTrackHeight(int value) {
        assign(trackHeight_, std::clamp(value, kMinTrackHeight, kMaxTrackHeight), &Nav::viewChanged);
    }
    void setScrollTicks(double value) { assign(scrollTicks_, std::max(0.0, value), &Nav::scrollChanged); }
    void setLaneWidth(double value) { assign(laneWidth_, value, &Nav::scrollChanged); }

    [[nodiscard]] nlohmann::json toJson() const {
        const auto strings = [](const QStringList& values) {
            nlohmann::json list = nlohmann::json::array();
            for (const QString& value : values) {
                list.push_back(value.toStdString());
            }
            return list;
        };
        return {{"page", enumKey(page_)},
                {"settingsTab", settingsTab_.toStdString()},
                {"selectedTrack", selectedTrack_.toStdString()},
                {"selectedClips", strings(selectedClips_)},
                {"selectedMarker", selectedMarker_.toStdString()},
                {"pixelsPerTick", pixelsPerTick_},
                {"trackHeight", trackHeight_},
                {"scrollTicks", scrollTicks_},
                {"paletteOpen", paletteOpen_},
                {"aboutOpen", aboutOpen_},
                {"inspectorOpen", inspectorOpen_},
                {"projectMenuOpen", projectMenuOpen_},
                {"consoleOpen", consoleOpen_},
                {"mixerOpen", mixerOpen_},
                {"metronomeMenuOpen", metronomeMenuOpen_},
                {"pianoRollOpen", pianoRollOpen_},
                {"pianoRollClip", pianoRollClip_.toStdString()},
                {"selectedNotes", strings(selectedNotes_)},
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
    QString renamingTrack_;
    QString selectedMarker_;
    QString renamingMarker_;
    QString trackMenu_;
    bool paletteOpen_ = false;
    bool aboutOpen_ = false;
    bool inspectorOpen_ = true;
    bool projectMenuOpen_ = false;
    bool consoleOpen_ = false;
    bool mixerOpen_ = false;
    bool metronomeMenuOpen_ = false;
    bool pianoRollOpen_ = false;
    QString pianoRollClip_;
    QStringList selectedNotes_;
    int keyHeight_ = kDefaultKeyHeight;
    bool automationOpen_ = false;
    QVariantMap automationLanes_;
    QStringList selectedPoints_;
    QStringList selectedClips_;
    double pixelsPerTick_ = kDefaultPixelsPerTick;
    int trackHeight_ = kDefaultTrackHeight;
    double scrollTicks_ = 0;
    double laneWidth_ = 0;
};

}  // namespace emerick::app
