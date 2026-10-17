#include "emerick/app/qml_exposure.hpp"

#include <memory>

#include "emerick/app/clip_notes.hpp"
#include "emerick/app/enums.hpp"
#include "emerick/app/transport_clock.hpp"

namespace emerick::app {

void exposeToQml(QQmlEngine& engine, Registry& registry, Nav& nav, ProjectController& controller,
                 TrackListModel& tracks, PeakCache& peaks) {
    qmlRegisterUncreatableMetaObject(emerick::app::staticMetaObject, "Emerick.App", 1, 0, "App",
                                     "App only holds enums");
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Registry", &registry);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Nav", &nav);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Project", &controller);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Tracks", &tracks);
    // The engine owns the clock as its Qt parent.
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Clock",
                                 std::make_unique<TransportClock>(controller, &engine).release());
    qmlRegisterType<ClipNotes>("Emerick.App", 1, 0, "ClipNotes");
    // The engine takes ownership of the provider.
    engine.addImageProvider("peaks", std::make_unique<PeakImageProvider>(peaks).release());
}

}  // namespace emerick::app
