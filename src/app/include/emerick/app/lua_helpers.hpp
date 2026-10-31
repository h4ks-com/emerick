#pragma once

#include <string>

#include "emerick/app/registry.hpp"

namespace emerick::app {

// Adds a bus named name with one insert per {type = params} entry of inserts, in order, and returns {bus, inserts}.
// Checks every entry first, so a bad entry adds nothing.
Json addBus(const Registry& registry, const std::string& name, const Json& inserts);
// Adds a point per {bar, beat, value, curve?} entry of points to the lane of param on the track, given by id or name,
// and returns {points}. Checks every point first, so a bad point adds nothing.
Json automate(const Registry& registry, const std::string& track, const std::string& param, const Json& points);

}  // namespace emerick::app
