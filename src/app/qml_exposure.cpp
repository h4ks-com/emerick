#include "emerick/app/qml_exposure.hpp"

#include <memory>

#include "emerick/app/clip_notes.hpp"
#include "emerick/app/code_highlighter.hpp"
#include "emerick/app/enums.hpp"
#include "emerick/app/transport_clock.hpp"
#include "emerick/app/waveform.hpp"

namespace emerick::app {

void exposeToQml(QQmlEngine& engine, Registry& registry, Nav& nav, ProjectController& controller,
                 TrackListModel& tracks, PeakCache& peaks, Console* console) {
    qmlRegisterUncreatableMetaObject(emerick::app::staticMetaObject, "Emerick.App", 1, 0, "App",
                                     "App only holds enums");
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Registry", &registry);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Nav", &nav);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Project", &controller);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Tracks", &tracks);
    // The engine owns the clock as its Qt parent.
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Clock",
                                 std::make_unique<TransportClock>(controller, &engine).release());
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Peaks", &peaks);
    if (console != nullptr) {
        qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Console", console);
    }
    qmlRegisterType<ClipNotes>("Emerick.App", 1, 0, "ClipNotes");
    qmlRegisterType<CodeHighlighter>("Emerick.App", 1, 0, "CodeHighlighter");
    qmlRegisterType<Waveform>("Emerick.App", 1, 0, "Waveform");
}

}  // namespace emerick::app
