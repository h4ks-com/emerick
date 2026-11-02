#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "emerick/model/document.hpp"

namespace emerick::viewer {

// The read-only half of the app's Project singleton: the document loaded from project.json and a playhead that the
// page's audio element moves. QML binds to it by the same names as to ProjectController.
class ViewerProject : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 positionTicks READ positionTicks NOTIFY transportChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY transportChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY transportChanged)
    Q_PROPERTY(bool recording READ recording CONSTANT)
    Q_PROPERTY(QString tempoText READ tempoText NOTIFY projectChanged)
    Q_PROPERTY(qint64 ticksPerBeat READ ticksPerBeat NOTIFY projectChanged)
    Q_PROPERTY(qint64 ticksPerBar READ ticksPerBar NOTIFY projectChanged)
    Q_PROPERTY(qint64 ticksPerQuarter READ ticksPerQuarter CONSTANT)
    Q_PROPERTY(qint64 endTicks READ endTicks NOTIFY projectChanged)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap cycle READ cycle NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap trackErrors READ trackErrors CONSTANT)
    Q_PROPERTY(int trackColorCount READ trackColorCount CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY projectChanged)

public:
    using QObject::QObject;

    [[nodiscard]] const model::Document& document() const { return document_; }
    void setDocument(model::Document document, QString name);

    [[nodiscard]] qint64 positionTicks() const { return positionTicks_; }
    [[nodiscard]] QString positionText() const;
    [[nodiscard]] bool playing() const { return playing_; }
    void setPlaying(bool playing);
    [[nodiscard]] static bool recording() { return false; }
    [[nodiscard]] QString tempoText() const;
    [[nodiscard]] qint64 ticksPerBeat() const { return document_.tempo.ticksPerBeat(); }
    [[nodiscard]] qint64 ticksPerBar() const { return document_.tempo.ticksPerBar(); }
    [[nodiscard]] static qint64 ticksPerQuarter() { return core::kTicksPerQuarter; }
    [[nodiscard]] qint64 endTicks() const;
    [[nodiscard]] QVariantList markers() const;
    [[nodiscard]] QVariantMap cycle() const;
    [[nodiscard]] static QVariantMap trackErrors() { return {}; }
    [[nodiscard]] static int trackColorCount() { return kTrackColors; }
    [[nodiscard]] QString name() const { return name_; }

    void setPositionSeconds(double seconds);
    [[nodiscard]] double positionSeconds() const { return document_.tempo.ticksToSeconds(positionTicks_); }

    Q_INVOKABLE static qint64 placed(qint64 ticks, int modifiers);
    Q_INVOKABLE QString ticksText(qint64 ticks, bool duration) const;
    Q_INVOKABLE static QVariantList fadeCurve(const QString& shape, double curve);
    Q_INVOKABLE static double curveThroughMidpoint(const QString& shape, double gain);

    static constexpr int kTrackColors = 8;

signals:
    void transportChanged();
    void projectChanged();

private:
    model::Document document_;
    QString name_;
    qint64 positionTicks_ = 0;
    bool playing_ = false;
};

}  // namespace emerick::viewer
