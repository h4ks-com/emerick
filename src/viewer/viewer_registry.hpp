#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <vector>

#include "emerick/app/nav.hpp"
#include "viewer_project.hpp"

namespace emerick::viewer {

// The parts of the app's Registry singleton the tracks view reads. It runs only the actions that look around the
// project: move the playhead, play or pause the mix, and show automation lanes. Every edit is off.
class ViewerRegistry : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap settings MEMBER settings_ CONSTANT)
    Q_PROPERTY(QVariantMap menus READ menus CONSTANT)
    Q_PROPERTY(bool gesturing READ gesturing CONSTANT)
    Q_PROPERTY(bool readOnly READ readOnly CONSTANT)

public:
    ViewerRegistry(const ViewerProject& project, app::Nav& nav, QObject* parent = nullptr);

    [[nodiscard]] static QVariantMap menus() { return {}; }
    [[nodiscard]] static bool gesturing() { return false; }
    [[nodiscard]] static bool readOnly() { return true; }

    Q_INVOKABLE static QVariantMap actionInfo(const QString& id);
    Q_INVOKABLE static QString currentChoice(const QString& /*id*/, const QVariantMap& /*params*/) { return {}; }
    Q_INVOKABLE void activate(const QString& id);
    Q_INVOKABLE QVariant runAction(const QString& id, const QVariantMap& params);
    Q_INVOKABLE static void runActions(const QString& /*name*/, const QVariantList& /*calls*/) {}
    Q_INVOKABLE static void updateGesture(const QString& /*name*/, const QVariantList& /*calls*/) {}
    Q_INVOKABLE static void endGesture() {}
    Q_INVOKABLE static void cancelGesture() {}
    Q_INVOKABLE static QString localPath(const QUrl& file) { return file.toLocalFile(); }
    Q_INVOKABLE static bool setSettingValue(const QString& /*id*/, const QVariant& /*value*/) { return false; }

    void locate(const QVariantMap& params);
    void togglePlay(const QVariantMap& params);
    void toggleAutomation(const QVariantMap& params);
    void showLane(const QVariantMap& params);
    void previousMarker(const QVariantMap& params);
    void nextMarker(const QVariantMap& params);

signals:
    // The page's audio element plays the mix, so the playhead moves only when it seeks.
    void seekRequested(double seconds);
    void playToggled();

private:
    void seekToTicks(qint64 ticks);
    [[nodiscard]] std::vector<core::Ticks> markerTicks() const;

    const ViewerProject& project_;
    app::Nav& nav_;
    QVariantMap settings_{{"view.headerWidth", 240}, {"edit.snap", true}};
};

}  // namespace emerick::viewer
