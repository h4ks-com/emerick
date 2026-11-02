#pragma once

#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace emerick::core {

// The libc++ of Xcode 26.2, the newest on Intel macOS runners, has no std::views::join_with.
template <std::ranges::input_range Parts>
std::string join(Parts&& parts, std::string_view separator) {
    std::string joined;
    bool first = true;
    for (auto&& part : std::forward<Parts>(parts)) {
        if (!first) {
            joined += separator;
        }
        joined += part;
        first = false;
    }
    return joined;
}

}  // namespace emerick::core
