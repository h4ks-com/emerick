#include "emerick/yamaha/arranger_state.hpp"

#include <nlohmann/json.hpp>

#include "emerick/core/base64.hpp"

namespace emerick::yamaha {

ArrangerState parseArrangerState(std::string_view text) {
    const nlohmann::json state = nlohmann::json::parse(text, nullptr, false);
    if (!state.is_object()) {
        return {};
    }
    const auto string = [&](const char* key) {
        const auto found = state.find(key);
        return found != state.end() && found->is_string() ? found->get<std::string>() : std::string{};
    };
    const auto autoFill = state.find("autoFill");
    return {.style = string("style"),
            .hash = string("hash"),
            .autoFill = autoFill == state.end() || !autoFill->is_boolean() || autoFill->get<bool>(),
            .data = core::fromBase64(string("data")).value_or(std::vector<std::uint8_t>{})};
}

}  // namespace emerick::yamaha
