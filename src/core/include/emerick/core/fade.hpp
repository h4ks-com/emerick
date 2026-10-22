#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace emerick::core {

enum class FadeShape : std::uint8_t { linear, equalPower, exponential, logarithmic, sCurve };

struct FadeShapeName {
    FadeShape shape;
    std::string_view id;
    std::string_view title;
};

inline constexpr std::array kFadeShapes{
    FadeShapeName{FadeShape::linear, "linear", "Linear"},
    FadeShapeName{FadeShape::equalPower, "equalPower", "Equal power"},
    FadeShapeName{FadeShape::exponential, "exponential", "Exponential"},
    FadeShapeName{FadeShape::logarithmic, "logarithmic", "Logarithmic"},
    FadeShapeName{FadeShape::sCurve, "sCurve", "S-curve"},
};

std::optional<FadeShape> fadeShapeFromId(std::string_view id);
std::string_view fadeShapeId(FadeShape shape);

// A shape bent by a curve from -1 to 1: a positive curve raises the middle of the fade, a negative one lowers it.
// The weight is the curve already raised to its power, so the audio thread only multiplies.
struct FadeCurve {
    FadeShape shape = FadeShape::linear;
    float weight = 1.0F;
};

FadeCurve fadeCurve(FadeShape shape, double curve);

// The gain of a fade in at progress t from 0 to 1; a fade out plays the gain at the fraction it has left.
float fadeCurveGain(FadeCurve curve, float t);

// The curve that makes the shape pass through gain at its midpoint, so a vertical drag follows the pointer.
double curveThroughMidpoint(FadeShape shape, double gain);

}  // namespace emerick::core
