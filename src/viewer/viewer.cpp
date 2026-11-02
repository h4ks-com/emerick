#include "viewer.hpp"

#include <QQmlEngine>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "emerick/app/clip_notes.hpp"
#include "emerick/app/control_curve.hpp"
#include "emerick/app/enums.hpp"
#include "emerick/app/waveform.hpp"
#include "emerick/model/json.hpp"

namespace emerick::viewer {

Viewer::Viewer() {
    QObject::connect(&registry, &ViewerRegistry::seekRequested, &project,
                     [this](double seconds) { project.setPositionSeconds(seconds); });
}

void Viewer::exposeToQml() {
    qmlRegisterUncreatableMetaObject(app::staticMetaObject, "Emerick.App", 1, 0, "App", "App only holds enums");
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Registry", &registry);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Nav", &nav);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Project", &project);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Tracks", &tracks);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Meters", &meters);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Peaks", &peaks);
    qmlRegisterType<app::ClipNotes>("Emerick.App", 1, 0, "ClipNotes");
    qmlRegisterType<app::ControlCurve>("Emerick.App", 1, 0, "ControlCurve");
    qmlRegisterType<app::Waveform>("Emerick.App", 1, 0, "Waveform");
}

QString Viewer::load(const QByteArray& json, const QString& name) {
    try {
        project.setDocument(model::documentFromJson(nlohmann::json::parse(json.toStdString())), name);
    } catch (const nlohmann::json::exception& error) {
        return QString::fromUtf8(error.what());
    } catch (const std::runtime_error& error) {
        return QString::fromUtf8(error.what());
    }
    return {};
}

}  // namespace emerick::viewer
