#pragma once

#include <QObject>
#include <QString>
#include <nlohmann/json.hpp>

#include "emerick/app/enums.hpp"

namespace emerick::app {

class Nav : public QObject {
    Q_OBJECT
    Q_PROPERTY(emerick::app::Page page READ page WRITE setPage NOTIFY changed)
    Q_PROPERTY(emerick::app::SettingsTab settingsTab READ settingsTab WRITE setSettingsTab NOTIFY changed)
    Q_PROPERTY(QString selectedTrack READ selectedTrack WRITE setSelectedTrack NOTIFY changed)
    Q_PROPERTY(bool paletteOpen READ paletteOpen WRITE setPaletteOpen NOTIFY changed)
    Q_PROPERTY(bool aboutOpen READ aboutOpen WRITE setAboutOpen NOTIFY changed)

public:
    using QObject::QObject;

    [[nodiscard]] Page page() const { return page_; }
    [[nodiscard]] SettingsTab settingsTab() const { return settingsTab_; }
    [[nodiscard]] QString selectedTrack() const { return selectedTrack_; }
    [[nodiscard]] bool paletteOpen() const { return paletteOpen_; }
    [[nodiscard]] bool aboutOpen() const { return aboutOpen_; }

    void setPage(Page value) { assign(page_, value); }
    void setSettingsTab(SettingsTab value) { assign(settingsTab_, value); }
    void setSelectedTrack(const QString& value) { assign(selectedTrack_, value); }
    void setPaletteOpen(bool value) { assign(paletteOpen_, value); }
    void setAboutOpen(bool value) { assign(aboutOpen_, value); }

    [[nodiscard]] nlohmann::json toJson() const {
        return {{"page", enumKey(page_)},
                {"settingsTab", enumKey(settingsTab_)},
                {"selectedTrack", selectedTrack_.toStdString()},
                {"paletteOpen", paletteOpen_},
                {"aboutOpen", aboutOpen_}};
    }

signals:
    void changed();

private:
    template <class T>
    void assign(T& field, const T& value) {
        if (field != value) {
            field = value;
            emit changed();
        }
    }

    Page page_ = Page::Main;
    SettingsTab settingsTab_ = SettingsTab::General;
    QString selectedTrack_;
    bool paletteOpen_ = false;
    bool aboutOpen_ = false;
};

}  // namespace emerick::app
