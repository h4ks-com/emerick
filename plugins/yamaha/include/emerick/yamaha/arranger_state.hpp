#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace emerick::yamaha {

// The Arranger device's state on a Style track: the style file, the hash of its bytes, Auto fill, and the bytes
// themselves up to arranger::kEmbeddedStyleLimit, so project.json alone plays the style.
struct ArrangerState {
    std::string style;
    std::string hash;
    bool autoFill = true;
    std::vector<std::uint8_t> data;
};

// The state the Arranger saved; text that is no JSON object reads as a new Arranger's state.
ArrangerState parseArrangerState(std::string_view text);

}  // namespace emerick::yamaha
