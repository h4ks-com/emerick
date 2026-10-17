#pragma once

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

}  // namespace emerick::app
