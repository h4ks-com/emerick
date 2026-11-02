#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "emerick/core/lookup.hpp"

namespace emerick::core {

// The curve a point's segment takes to the next point: a straight line, the point's value held until the next point,
// or an S-shaped ease.
enum class AutomationCurve : std::uint8_t { linear, hold, smooth };

// The parameter a lane moves. Gain and send levels are in dB, pan runs from -1 to 1 and mute is on from 0.5.
enum class AutomationTarget : std::uint8_t { gain, pan, mute, send, insert };

struct AutomationCurveName {
    AutomationCurve curve;
    std::string_view id;
    std::string_view title;
};

inline constexpr std::array kAutomationCurves{AutomationCurveName{AutomationCurve::linear, "linear", "Linear"},
                                              AutomationCurveName{AutomationCurve::hold, "hold", "Hold"},
                                              AutomationCurveName{AutomationCurve::smooth, "smooth", "Smooth"}};

constexpr std::optional<AutomationCurve> automationCurveFromId(std::string_view id) {
    const auto* found = findEntry(kAutomationCurves, id, &AutomationCurveName::id);
    return found == nullptr ? std::nullopt : std::optional{found->curve};
}

constexpr std::string_view automationCurveId(AutomationCurve curve) {
    return std::ranges::find(kAutomationCurves, curve, &AutomationCurveName::curve)->id;
}

// The value at progress t from 0 to 1 along a segment from one point's value to the next.
constexpr double automationBetween(double from, double to, double t, AutomationCurve curve) {
    switch (curve) {
        case AutomationCurve::linear:
            return from + ((to - from) * t);
        case AutomationCurve::hold:
            return from;
        case AutomationCurve::smooth:
            return from + ((to - from) * t * t * (3.0 - (2.0 * t)));
    }
    return from;
}

}  // namespace emerick::core
