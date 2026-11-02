#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "emerick/core/automation.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

inline constexpr double kAutomationGainMinDb = -60.0;
inline constexpr double kAutomationGainMaxDb = 6.0;

enum class LaneScale : std::uint8_t { linear, logarithmic, fader };

// A parameter a lane can automate. The value is the track's own setting, which the lane starts from.
struct AutomationParam {
    std::string id;
    std::string title;
    double minimum = 0.0;
    double maximum = 1.0;
    double value = 0.0;
    bool toggle = false;
    core::AutomationTarget target = core::AutomationTarget::gain;
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

// The track's gain, pan, mute and send levels and the params of its built-in inserts, in the order the lane picker
// lists them.
std::vector<AutomationParam> trackParams(const model::Document& document, const model::Track& track);

}  // namespace emerick::app
