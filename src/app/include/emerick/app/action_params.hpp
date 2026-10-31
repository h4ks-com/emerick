#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "emerick/app/params.hpp"
#include "emerick/engine/effects.hpp"
#include "emerick/model/project.hpp"

namespace emerick::app {

model::TrackId findTrackByIdOrName(const model::Project& project, const std::string& idOrName);
// Accepts a track id or an exact track name.
model::TrackId trackParam(const model::Project& project, const Json& params, const std::string& key = "track");
model::ClipId clipParam(const model::Project& project, const Json& params, const std::string& key = "clip");
// Accepts ticks or a "bar.beat.tick" string.
std::optional<core::Ticks> optionalPosition(const Json& params, const std::string& key, const core::TempoMap& tempo);
core::Ticks positionParam(const Json& params, const std::string& key, const core::TempoMap& tempo);

// The index of the choice the text names: the exact match, else the only case-insensitive match. Throws AppError naming
// up to three close choices when there is neither, and hint, such as where to read the choices.
std::size_t choiceIndex(std::string_view name, std::span<const std::string_view> choices, std::string_view text,
                        std::string_view hint);

const engine::EffectSpec& effectType(const std::string& type);
const engine::EffectParam& effectParamNamed(const engine::EffectSpec& spec, const std::string& name);
// Toggles store 0 or 1 and choices a whole index; a choice also accepts its name, such as "1/8".
double effectParamValue(const engine::EffectParam& described, const Json& value);

inline double rounded(double value, double step) { return std::round(value / step) * step; }
// A level in dB to a tenth, or null for silence.
inline Json levelDb(double value) { return std::isfinite(value) ? Json(rounded(value, 0.1)) : Json(nullptr); }

}  // namespace emerick::app
