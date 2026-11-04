#pragma once

#include <QQmlEngine>

#include "emerick/app/console.hpp"
#include "emerick/app/libraries.hpp"
#include "emerick/app/monitor_actions.hpp"
#include "emerick/app/nav.hpp"
#include "emerick/app/peak_cache.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/app/track_list_model.hpp"

namespace emerick::app {

// Publishes the Emerick.App singletons and types, and serves waveforms to the engine. Call it once per process. The
// console panel, the monitor windows and the libraries dialog load only when opened, so a view test can leave them out.
void exposeToQml(QQmlEngine& engine, Registry& registry, Nav& nav, ProjectController& controller,
                 TrackListModel& tracks, PeakCache& peaks, Console* console = nullptr, Monitors* monitors = nullptr,
                 Libraries* libraries = nullptr);

}  // namespace emerick::app
