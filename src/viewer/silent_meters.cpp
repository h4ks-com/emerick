#include "silent_meters.hpp"

#include <algorithm>

namespace emerick::viewer {

double SilentMeters::fraction(double db) { return std::clamp((db - floorDb()) / -floorDb(), 0.0, 1.0); }

}  // namespace emerick::viewer
