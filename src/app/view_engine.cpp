#include "emerick/app/view_engine.hpp"

#include "emerick/app/qml_exposure.hpp"

namespace emerick::app {

ViewEngine::ViewEngine(Registry& registry, Nav& nav, ProjectController& controller)
    : registry_(registry), nav_(nav), controller_(controller) {}

ViewEngine::~ViewEngine() = default;

void ViewEngine::adopt(QQmlEngine& engine, PeakCache& peaks) {
    engine_ = &engine;
    peaks_ = &peaks;
}

QQmlEngine& ViewEngine::engine() {
    if (engine_ == nullptr) {
        ownTracks_ = std::make_unique<TrackListModel>(controller_, registry_);
        ownPeaks_ = std::make_unique<PeakCache>();
        ownEngine_ = std::make_unique<QQmlEngine>();
        exposeToQml(*ownEngine_, registry_, nav_, controller_, *ownTracks_, *ownPeaks_);
        adopt(*ownEngine_, *ownPeaks_);
    }
    return *engine_;
}

PeakCache& ViewEngine::peaks() {
    engine();
    return *peaks_;
}

}  // namespace emerick::app
