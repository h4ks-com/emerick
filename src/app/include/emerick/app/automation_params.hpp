#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

inline constexpr double kAutomationGainMinDb = -60.0;
inline constexpr double kAutomationGainMaxDb = 6.0;

enum class LaneScale : std::uint8_t { linear, logarithmic, fader };

// The live plugin of a plugin insert, or null.
using PluginLookup = std::function<std::shared_ptr<engine::Plugin>(const model::InsertId&)>;

// A parameter a lane can automate. The value is the track's own setting, which the lane starts from.
struct AutomationParam {
    std::string id;
    std::string title;
    double minimum = 0.0;
    double maximum = 1.0;
    double value = 0.0;
    bool toggle = false;
    engine::AutomationTarget target = engine::AutomationTarget::gain;
    std::string bus;
    // How the lane writes values: "dB", "pan" as L, C and R, or plain numbers.
    std::string unit;
    // An insert param names its insert and its index in the effect's table, or a plugin insert's param id.
    std::string insert;
    std::size_t insertParam = 0;
    bool plugin = false;
    // How a lane spreads the range over its height: a frequency by ratios, a gain on a fader taper.
    LaneScale scale = LaneScale::linear;
};

std::string sendParamId(const model::TrackId& bus);

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
