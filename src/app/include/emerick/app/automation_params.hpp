#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/app/track_params.hpp"
#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// The live plugin of a plugin insert, or null.
using PluginLookup = std::function<std::shared_ptr<engine::Plugin>(const model::InsertId&)>;

// Every parameter the track can automate, in the order the lane picker lists them, with the params of its plugin
// inserts as their live plugins list them. Only tracks that play audio have any.
std::vector<AutomationParam> automationParams(const Registry& registry, const model::Document& document,
                                              const model::Track& track, const PluginLookup& plugins);
std::optional<AutomationParam> automationParam(const Registry& registry, const model::Document& document,
                                               const model::Track& track, const PluginLookup& plugins,
                                               std::string_view id);

// The lanes the engine plays for the track's own params and built-in inserts: none in Off, and in every other mode
// the lanes with points whose parameter still resolves.
std::vector<engine::AutomationLane> engineLanes(const Registry& registry, const model::Document& document,
                                                const model::Track& track);
// The same for the params of the track's plugin inserts, with each value as the plugin's process call takes it.
std::vector<engine::AutomationLane> pluginLanes(const model::Track& track, const PluginLookup& plugins);

}  // namespace emerick::app
