#include "emerick/engine/effects.hpp"

#include <algorithm>

namespace emerick::engine {

const EffectSpec& effectSpec(EffectType type) { return kEffects.at(static_cast<std::size_t>(type)); }

const EffectSpec* findEffect(std::string_view id) {
    const auto* const found = std::ranges::find(kEffects, id, &EffectSpec::id);
    return found == kEffects.end() ? nullptr : found;
}

std::optional<std::size_t> paramIndex(const EffectSpec& spec, std::string_view id) {
    const auto found = std::ranges::find(spec.params, id, &EffectParam::id);
    if (found == spec.params.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - spec.params.begin());
}

EffectParams defaultParams(const EffectSpec& spec) {
    EffectParams params{};
    std::ranges::transform(spec.params, params.begin(), &EffectParam::defaultValue);
    return params;
}

}  // namespace emerick::engine
