#pragma once

#include <compare>
#include <string>

namespace emerick::core {

std::string newIdValue();

// We give each id its own Tag, so the compiler rejects a ClipId where a TrackId belongs.
template <class Tag>
struct Id {
    std::string value;

    static Id make() { return Id{newIdValue()}; }
    [[nodiscard]] bool empty() const { return value.empty(); }
    auto operator<=>(const Id&) const = default;
};

}  // namespace emerick::core

template <class Tag>
struct std::hash<emerick::core::Id<Tag>> {
    std::size_t operator()(const emerick::core::Id<Tag>& id) const noexcept {
        return std::hash<std::string>{}(id.value);
    }
};
