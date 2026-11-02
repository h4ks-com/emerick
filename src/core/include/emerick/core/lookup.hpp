#pragma once

#include <array>
#include <cstddef>
#include <functional>

namespace emerick::core {

// The first entry of a constant table whose projected field equals value, or null. We return a pointer because some
// standard libraries, such as emscripten's, give std::array iterators that are no pointers.
template <typename Entry, std::size_t Size, typename Value, typename Projection>
constexpr const Entry* findEntry(const std::array<Entry, Size>& table, const Value& value, Projection projection) {
    for (const Entry& entry : table) {
        if (std::invoke(projection, entry) == value) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace emerick::core
