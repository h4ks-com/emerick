#pragma once

#include <string>

#include "emerick/app/background_render.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::app {

inline constexpr const char* kMixInspectAction = "mix.inspect";

std::string loudnessMessage(double integratedLufs, double truePeakDbtp, const std::string& detail);

// UI thread: checks track.analyze's params and sets up the render of its track and its measurement, keyed by its id.
RenderJob prepareTrackRender(ProjectController& controller, const Registry& registry, const Json& params);
// UI thread: checks tracks.analyze's params and sets up one render of every analyzable track and their measurements,
// keyed by track id.
RenderJob prepareTracksRender(ProjectController& controller, const Registry& registry, const Json& params);
// UI thread: checks mix.inspect's params and sets up one render of the mix and every analyzable track, and the issues
// found in them.
RenderJob prepareMixInspection(ProjectController& controller, const Registry& registry, const Json& params);

}  // namespace emerick::app
