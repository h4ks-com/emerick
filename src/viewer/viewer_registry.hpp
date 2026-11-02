#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <vector>

#include "emerick/app/nav.hpp"
#include "viewer_project.hpp"

namespace emerick::viewer {

// The parts of the app's Registry singleton the views read. It runs only the actions that look around the project,
// with the app's own titles and keys: move the playhead, play, pause or stop the mix, zoom, and show automation.
// Every edit is off.
class ViewerRegistry : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap settings MEMBER settings_ CONSTANT)
    Q_PROPERTY(QVariantMap menus READ menus CONSTANT)
    Q_PROPERTY(QVariantList shortcuts READ shortcuts CONSTANT)
    Q_PROPERTY(bool gesturing READ gesturing CONSTANT)
    Q_PROPERTY(bool readOnly READ readOnly CONSTANT)

public:
    ViewerRegistry(const ViewerProject& project, app::Nav& nav, QObject* parent = nullptr);

    [[nodiscard]] static QVariantMap menus() { return {}; }
    [[nodiscard]] QVariantList shortcuts() const;
    [[nodiscard]] static bool gesturing() { return false; }
    [[nodiscard]] static bool readOnly() { return true; }

    Q_INVOKABLE QVariantMap actionInfo(const QString& id) const;
    Q_INVOKABLE static QString currentChoice(const QString& /*id*/, const QVariantMap& /*params*/) { return {}; }
    Q_INVOKABLE void activate(const QString& id);
    Q_INVOKABLE QVariant runAction(const QString& id, const QVariantMap& params);
    Q_INVOKABLE static void runActions(const QString& /*name*/, const QVariantList& /*calls*/) {}
    Q_INVOKABLE static void updateGesture(const QString& /*name*/, const QVariantList& /*calls*/) {}
    Q_INVOKABLE static void endGesture() {}
    Q_INVOKABLE static void cancelGesture() {}
    Q_INVOKABLE static QString localPath(const QUrl& file) { return file.toLocalFile(); }
    Q_INVOKABLE static bool setSettingValue(const QString& /*id*/, const QVariant& /*value*/) { return false; }

signals:
    // The page's audio element plays the mix, so the playhead moves only when it seeks.
    void seekRequested(double seconds);
    void playToggled();
    void pauseRequested();

private:
    struct Action {
        QString id;
        QString title;
        QStringList shortcuts;
        std::function<void(const QVariantMap&)> run;
    };

    void addAction(const char* id, const char* title, QStringList shortcuts,
                   std::function<void(const QVariantMap&)> run);
    [[nodiscard]] const Action* action(const QString& id) const;
    void seekToTicks(qint64 ticks);
    void stepToMarker(int step);

    const ViewerProject& project_;
    app::Nav& nav_;
    std::vector<Action> actions_;
    QVariantMap settings_{{"view.headerWidth", 240}, {"edit.snap", true}, {"metronome.enabled", false}};
};

}  // namespace emerick::viewer
