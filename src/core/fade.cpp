#include "emerick/core/fade.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace emerick::core {

namespace {

constexpr double kCurveOctaves = 4.0;

float shapeGain(FadeShape shape, float t) {
    switch (shape) {
        case FadeShape::linear:
            return t;
        case FadeShape::equalPower:
            return std::sin(t * std::numbers::pi_v<float> / 2.0F);
        case FadeShape::exponential:
            return t * t * t;
        case FadeShape::logarithmic:
            return 1.0F - (1.0F - t) * (1.0F - t) * (1.0F - t);
        case FadeShape::sCurve:
            return 0.5F - 0.5F * std::cos(t * std::numbers::pi_v<float>);
    }
    return t;
}

}  // namespace

std::optional<FadeShape> fadeShapeFromId(std::string_view id) {
    const auto* const found = std::ranges::find(kFadeShapes, id, &FadeShapeName::id);
    return found == kFadeShapes.end() ? std::nullopt : std::optional{found->shape};
}

std::string_view fadeShapeId(FadeShape shape) {
    return std::ranges::find(kFadeShapes, shape, &FadeShapeName::shape)->id;
}

FadeCurve fadeCurve(FadeShape shape, double curve) {
    const double bend = std::isfinite(curve) ? std::clamp(curve, -1.0, 1.0) : 0.0;
    return {shape, static_cast<float>(std::exp2(bend * kCurveOctaves))};
}

float fadeCurveGain(FadeCurve curve, float t) {
    const float base = shapeGain(curve.shape, std::clamp(t, 0.0F, 1.0F));
    if (base <= 0.0F || base >= 1.0F) {
        return std::clamp(base, 0.0F, 1.0F);
    }
    const float weighted = base * curve.weight;
    return weighted / (weighted + 1.0F - base);
}

double curveThroughMidpoint(FadeShape shape, double gain) {
    const double target = std::clamp(gain, 0.001, 0.999);
    const double base = shapeGain(shape, 0.5F);
    return std::clamp((std::log2(target / (1.0 - target)) - std::log2(base / (1.0 - base))) / kCurveOctaves, -1.0, 1.0);
}

}  // namespace emerick::core
