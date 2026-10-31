#pragma once

#include <string>

#include "emerick/app/registry.hpp"

namespace emerick::app {

// Adds a bus named name with one insert per {type = params} entry of inserts, in order, and returns {bus, inserts}.
Json addBus(const Registry& registry, const std::string& name, const Json& inserts);
// Adds a point per {bar, beat, value, curve?} entry of points to the track's lane of param and returns {points}.
Json automate(const Registry& registry, const std::string& track, const std::string& param, const Json& points);
std::string trackIdByName(const Registry& registry, const std::string& name);

}  // namespace emerick::app
