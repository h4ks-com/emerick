#pragma once

#include <QQmlEngine>
#include <memory>

#include "emerick/app/nav.hpp"
#include "emerick/app/peak_cache.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/app/track_list_model.hpp"

namespace emerick::app {

// The QML engine that offscreen views load into: the window's engine, or with no window one made on first use, since
// the Emerick.App singletons register once per process. It also saves the timeline view with the project and restores
// it when a project opens, so every way of running the app opens a project as it was saved.
class ViewEngine {
public:
    ViewEngine(Registry& registry, Nav& nav, ProjectController& controller);
    ~ViewEngine();
    ViewEngine(const ViewEngine&) = delete;
    ViewEngine& operator=(const ViewEngine&) = delete;
    ViewEngine(ViewEngine&&) = delete;
    ViewEngine& operator=(ViewEngine&&) = delete;

    // Uses an engine that exposeToQml already published the singletons to, with the peaks it serves.
    void adopt(QQmlEngine& engine, PeakCache& peaks);
    QQmlEngine& engine();
    PeakCache& peaks();

private:
    Registry& registry_;
    Nav& nav_;
    ProjectController& controller_;
    QQmlEngine* engine_ = nullptr;
    PeakCache* peaks_ = nullptr;
    std::unique_ptr<TrackListModel> ownTracks_;
    std::unique_ptr<PeakCache> ownPeaks_;
    // Declared last, so the engine goes before the objects its singletons point to.
    std::unique_ptr<QQmlEngine> ownEngine_;
};

}  // namespace emerick::app
