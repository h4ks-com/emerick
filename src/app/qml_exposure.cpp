#include "emerick/app/qml_exposure.hpp"

#include <memory>

#include "emerick/app/clip_notes.hpp"
#include "emerick/app/code_highlighter.hpp"
#include "emerick/app/control_curve.hpp"
#include "emerick/app/effect_catalog.hpp"
#include "emerick/app/enums.hpp"
#include "emerick/app/spectrogram_view.hpp"
#include "emerick/app/track_analysis.hpp"
#include "emerick/app/transport_clock.hpp"
#include "emerick/app/voice_audition.hpp"
#include "emerick/app/waveform.hpp"

namespace emerick::app {

void exposeToQml(QQmlEngine& engine, Registry& registry, Nav& nav, ProjectController& controller,
                 TrackListModel& tracks, PeakCache& peaks, Console* console, Monitors* monitors, Libraries* libraries) {
    qmlRegisterUncreatableMetaObject(emerick::app::staticMetaObject, "Emerick.App", 1, 0, "App",
                                     "App only holds enums");
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Registry", &registry);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Nav", &nav);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Project", &controller);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Tracks", &tracks);
    // The engine owns the clock as its Qt parent.
    auto* clock = std::make_unique<TransportClock>(&engine).release();
    const auto showPosition = [clock, &controller] {
        clock->show(controller.project().document().tempo, controller.positionTicks());
    };
    QObject::connect(&controller, &ProjectController::transportChanged, clock, showPosition);
    QObject::connect(&controller, &ProjectController::projectChanged, clock, showPosition);
    showPosition();
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Clock", clock);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Effects", std::make_unique<EffectCatalog>(&engine).release());
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Analysis",
                                 std::make_unique<TrackAnalysis>(registry, controller, &engine).release());
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "VoicePreview",
                                 std::make_unique<VoicePreview>(registry, controller, &engine).release());
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Peaks", &peaks);
    QObject::connect(&controller, &ProjectController::takePeaks, &peaks, &PeakCache::addLive);
    QObject::connect(&controller, &ProjectController::audioTakeLanded, &peaks, &PeakCache::forgetLive);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Meters", &controller.meters());
    if (console != nullptr) {
        qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Console", console);
    }
    if (monitors != nullptr) {
        qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Tuner", &monitors->tuner);
        qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Spectrogram", &monitors->spectrogram);
    }
    if (libraries != nullptr) {
        qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Libraries", libraries);
    }
    qmlRegisterType<SpectrogramView>("Emerick.App", 1, 0, "SpectrogramView");
    qmlRegisterType<ClipNotes>("Emerick.App", 1, 0, "ClipNotes");
    qmlRegisterType<CodeHighlighter>("Emerick.App", 1, 0, "CodeHighlighter");
    qmlRegisterType<ControlCurve>("Emerick.App", 1, 0, "ControlCurve");
    qmlRegisterType<Waveform>("Emerick.App", 1, 0, "Waveform");
}

}  // namespace emerick::app
