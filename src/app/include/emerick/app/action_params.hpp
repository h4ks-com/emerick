#pragma once

#include <cmath>
#include <optional>
#include <string>

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

const engine::EffectSpec& effectType(const std::string& type);
const engine::EffectParam& effectParamNamed(const engine::EffectSpec& spec, const std::string& name);
// Toggles store 0 or 1 and choices a whole index; a choice also accepts its name, such as "1/8".
double effectParamValue(const engine::EffectParam& described, const Json& value);

inline double rounded(double value, double step) { return std::round(value / step) * step; }
// A level in dB to a tenth, or null for silence.
inline Json levelDb(double value) { return std::isfinite(value) ? Json(rounded(value, 0.1)) : Json(nullptr); }

}  // namespace emerick::app
