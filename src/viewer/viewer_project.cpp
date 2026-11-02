#include "viewer_project.hpp"

#include <algorithm>
#include <utility>

#include "emerick/app/project_view.hpp"
#include "emerick/model/clip_edit.hpp"

namespace emerick::viewer {

void ViewerProject::setDocument(model::Document document, QString name) {
    document_ = std::move(document);
    name_ = std::move(name);
    emit projectChanged();
}

QString ViewerProject::positionText() const { return app::positionText(document_.tempo, positionTicks_); }

void ViewerProject::setPlaying(bool playing) {
    if (playing != playing_) {
        playing_ = playing;
        emit transportChanged();
    }
}

QString ViewerProject::tempoText() const { return app::tempoText(document_.tempo); }

qint64 ViewerProject::endTicks() const { return model::projectEnd(document_); }

QVariantList ViewerProject::markers() const { return app::toVariant(app::markersState(document_)).toList(); }

QVariantMap ViewerProject::cycle() const { return app::cycleEntry(document_.cycle); }

void ViewerProject::setPositionSeconds(double seconds) {
    const qint64 ticks = document_.tempo.secondsToTicks(std::max(0.0, seconds));
    if (ticks != positionTicks_) {
        positionTicks_ = ticks;
        emit transportChanged();
    }
}

qint64 ViewerProject::placed(qint64 ticks, int /*modifiers*/) { return ticks; }

QString ViewerProject::ticksText(qint64 ticks, bool duration) const {
    return app::ticksText(document_.tempo, ticks, duration);
}

QVariantList ViewerProject::fadeCurve(const QString& shape, double curve) { return app::fadeCurvePoints(shape, curve); }

double ViewerProject::curveThroughMidpoint(const QString& shape, double gain) {
    return app::fadeCurveThroughMidpoint(shape, gain);
}

}  // namespace emerick::viewer
