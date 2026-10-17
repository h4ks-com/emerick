#pragma once

#include <cstdint>
#include <vector>

#include "emerick/core/time.hpp"

namespace emerick::yamaha {

struct RawEvent {
    core::Ticks at = 0;
    std::vector<std::uint8_t> bytes;

    bool operator==(const RawEvent&) const = default;
};

}  // namespace emerick::yamaha
