#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
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

// Zoom and selection change often, so each has its own signal and a zoom never re-evaluates selection bindings.
class Nav : public QObject {
    Q_OBJECT
    Q_PROPERTY(emerick::app::Page page READ page WRITE setPage NOTIFY changed)
    Q_PROPERTY(QString settingsTab READ settingsTab WRITE setSettingsTab NOTIFY changed)
    Q_PROPERTY(QString selectedTrack READ selectedTrack WRITE setSelectedTrack NOTIFY changed)
    Q_PROPERTY(QString renamingTrack READ renamingTrack WRITE setRenamingTrack NOTIFY changed)
    Q_PROPERTY(QString trackMenu READ trackMenu WRITE setTrackMenu NOTIFY changed)
    Q_PROPERTY(bool paletteOpen READ paletteOpen WRITE setPaletteOpen NOTIFY changed)
    Q_PROPERTY(bool aboutOpen READ aboutOpen WRITE setAboutOpen NOTIFY changed)
    Q_PROPERTY(bool inspectorOpen READ inspectorOpen WRITE setInspectorOpen NOTIFY changed)
    Q_PROPERTY(bool projectMenuOpen READ projectMenuOpen WRITE setProjectMenuOpen NOTIFY changed)
    Q_PROPERTY(bool consoleOpen READ consoleOpen WRITE setConsoleOpen NOTIFY changed)
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
    // The track whose context menu is open.
    [[nodiscard]] QString trackMenu() const { return trackMenu_; }
    [[nodiscard]] bool paletteOpen() const { return paletteOpen_; }
    [[nodiscard]] bool aboutOpen() const { return aboutOpen_; }
    [[nodiscard]] bool inspectorOpen() const { return inspectorOpen_; }
    [[nodiscard]] bool projectMenuOpen() const { return projectMenuOpen_; }
    [[nodiscard]] bool consoleOpen() const { return consoleOpen_; }
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
    void setTrackMenu(const QString& value) { assign(trackMenu_, value, &Nav::changed); }
    void setPaletteOpen(bool value) { assign(paletteOpen_, value, &Nav::changed); }
    void setAboutOpen(bool value) { assign(aboutOpen_, value, &Nav::changed); }
    void setInspectorOpen(bool value) { assign(inspectorOpen_, value, &Nav::changed); }
    void setProjectMenuOpen(bool value) { assign(projectMenuOpen_, value, &Nav::changed); }
    void setConsoleOpen(bool value) { assign(consoleOpen_, value, &Nav::changed); }
    void setSelectedClips(const QStringList& value) { assign(selectedClips_, value, &Nav::selectionChanged); }
    void setPixelsPerTick(double value) {
        assign(pixelsPerTick_, std::clamp(value, kMinPixelsPerTick, kMaxPixelsPerTick), &Nav::viewChanged);
    }
    void setTrackHeight(int value) {
        assign(trackHeight_, std::clamp(value, kMinTrackHeight, kMaxTrackHeight), &Nav::viewChanged);
    }
    void setScrollTicks(double value) { assign(scrollTicks_, std::max(0.0, value), &Nav::scrollChanged); }
    void setLaneWidth(double value) { assign(laneWidth_, value, &Nav::scrollChanged); }

    [[nodiscard]] nlohmann::json toJson() const {
        nlohmann::json clips = nlohmann::json::array();
        for (const QString& clip : selectedClips_) {
            clips.push_back(clip.toStdString());
        }
        return {{"page", enumKey(page_)},
                {"settingsTab", settingsTab_.toStdString()},
                {"selectedTrack", selectedTrack_.toStdString()},
                {"selectedClips", clips},
                {"pixelsPerTick", pixelsPerTick_},
                {"trackHeight", trackHeight_},
                {"scrollTicks", scrollTicks_},
                {"paletteOpen", paletteOpen_},
                {"aboutOpen", aboutOpen_},
                {"inspectorOpen", inspectorOpen_},
                {"projectMenuOpen", projectMenuOpen_},
                {"consoleOpen", consoleOpen_}};
    }

signals:
    void changed();
    void selectionChanged();
    void viewChanged();
    void scrollChanged();

private:
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
    QString trackMenu_;
    bool paletteOpen_ = false;
    bool aboutOpen_ = false;
    bool inspectorOpen_ = true;
    bool projectMenuOpen_ = false;
    bool consoleOpen_ = false;
    QStringList selectedClips_;
    double pixelsPerTick_ = kDefaultPixelsPerTick;
    int trackHeight_ = kDefaultTrackHeight;
    double scrollTicks_ = 0;
    double laneWidth_ = 0;
};

}  // namespace emerick::app
