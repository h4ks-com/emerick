#pragma once

#include <cmath>
#include <optional>
#include <string>

#include "emerick/app/params.hpp"
#include "emerick/model/project.hpp"

namespace emerick::app {

model::TrackId findTrackByIdOrName(const model::Project& project, const std::string& idOrName);
// Accepts a track id or an exact track name.
model::TrackId trackParam(const model::Project& project, const Json& params, const std::string& key = "track");
model::ClipId clipParam(const model::Project& project, const Json& params, const std::string& key = "clip");
// Accepts ticks or a "bar.beat.tick" string.
std::optional<core::Ticks> optionalPosition(const Json& params, const std::string& key, const core::TempoMap& tempo);
core::Ticks positionParam(const Json& params, const std::string& key, const core::TempoMap& tempo);

inline double rounded(double value, double step) { return std::round(value / step) * step; }
// A level in dB to a tenth, or null for silence.
inline Json levelDb(double value) { return std::isfinite(value) ? Json(rounded(value, 0.1)) : Json(nullptr); }

}  // namespace emerick::app
