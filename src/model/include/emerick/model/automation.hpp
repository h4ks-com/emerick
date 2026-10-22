#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "emerick/model/document.hpp"

namespace emerick::model {

struct AutomationModeName {
    AutomationMode mode;
    std::string_view id;
    std::string_view title;
};

inline constexpr std::array kAutomationModes{AutomationModeName{AutomationMode::off, "off", "Off"},
                                             AutomationModeName{AutomationMode::read, "read", "Read"},
                                             AutomationModeName{AutomationMode::touch, "touch", "Touch"},
                                             AutomationModeName{AutomationMode::latch, "latch", "Latch"}};

std::optional<AutomationMode> automationModeFromId(std::string_view id);
std::string_view automationModeId(AutomationMode mode);

AutomationLane* findLane(Track& track, std::string_view param);
const AutomationLane* findLane(const Track& track, std::string_view param);
// The first point's value before it, the last point's after it and the segment's curve between them. A lane without
// points has no value.
std::optional<double> automationValueAt(const AutomationLane& lane, Ticks tick);
// Points on the same tick keep their order.
void sortPoints(AutomationLane& lane);

}  // namespace emerick::model
