#pragma once

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
};

std::string sendParamId(const model::TrackId& bus);

// Every parameter the track can automate, in the order the lane picker lists them. Only tracks that play audio have
// any.
std::vector<AutomationParam> automationParams(const Registry& registry, const model::Document& document,
                                              const model::Track& track);
std::optional<AutomationParam> automationParam(const Registry& registry, const model::Document& document,
                                               const model::Track& track, std::string_view id);

// The lanes the engine plays for the track: none in Off, and in every other mode the lanes with points whose
// parameter still resolves.
std::vector<engine::AutomationLane> engineLanes(const Registry& registry, const model::Document& document,
                                                const model::Track& track);

}  // namespace emerick::app
