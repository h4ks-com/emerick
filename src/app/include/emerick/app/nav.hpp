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
    Q_PROPERTY(bool paletteOpen READ paletteOpen WRITE setPaletteOpen NOTIFY changed)
    Q_PROPERTY(bool aboutOpen READ aboutOpen WRITE setAboutOpen NOTIFY changed)
    Q_PROPERTY(bool inspectorOpen READ inspectorOpen WRITE setInspectorOpen NOTIFY changed)
    Q_PROPERTY(QStringList selectedClips READ selectedClips WRITE setSelectedClips NOTIFY selectionChanged)
    Q_PROPERTY(double pixelsPerTick READ pixelsPerTick WRITE setPixelsPerTick NOTIFY viewChanged)
    Q_PROPERTY(int trackHeight READ trackHeight WRITE setTrackHeight NOTIFY viewChanged)

public:
    using QObject::QObject;

    [[nodiscard]] Page page() const { return page_; }
    [[nodiscard]] QString settingsTab() const { return settingsTab_; }
    [[nodiscard]] QString selectedTrack() const { return selectedTrack_; }
    [[nodiscard]] bool paletteOpen() const { return paletteOpen_; }
    [[nodiscard]] bool aboutOpen() const { return aboutOpen_; }
    [[nodiscard]] bool inspectorOpen() const { return inspectorOpen_; }
    [[nodiscard]] QStringList selectedClips() const { return selectedClips_; }
    [[nodiscard]] double pixelsPerTick() const { return pixelsPerTick_; }
    [[nodiscard]] int trackHeight() const { return trackHeight_; }

    void setPage(Page value) { assign(page_, value, &Nav::changed); }
    void setSettingsTab(const QString& value) { assign(settingsTab_, value, &Nav::changed); }
    void setSelectedTrack(const QString& value) { assign(selectedTrack_, value, &Nav::changed); }
    void setPaletteOpen(bool value) { assign(paletteOpen_, value, &Nav::changed); }
    void setAboutOpen(bool value) { assign(aboutOpen_, value, &Nav::changed); }
    void setInspectorOpen(bool value) { assign(inspectorOpen_, value, &Nav::changed); }
    void setSelectedClips(const QStringList& value) { assign(selectedClips_, value, &Nav::selectionChanged); }
    void setPixelsPerTick(double value) {
        assign(pixelsPerTick_, std::clamp(value, kMinPixelsPerTick, kMaxPixelsPerTick), &Nav::viewChanged);
    }
    void setTrackHeight(int value) {
        assign(trackHeight_, std::clamp(value, kMinTrackHeight, kMaxTrackHeight), &Nav::viewChanged);
    }

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
                {"paletteOpen", paletteOpen_},
                {"aboutOpen", aboutOpen_},
                {"inspectorOpen", inspectorOpen_}};
    }

signals:
    void changed();
    void selectionChanged();
    void viewChanged();

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
    bool paletteOpen_ = false;
    bool aboutOpen_ = false;
    bool inspectorOpen_ = true;
    QStringList selectedClips_;
    double pixelsPerTick_ = kDefaultPixelsPerTick;
    int trackHeight_ = kDefaultTrackHeight;
};

}  // namespace emerick::app
