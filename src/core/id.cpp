#include "emerick/core/id.hpp"

#include <array>
#include <random>

namespace emerick::core {

std::string newIdValue() {
    static thread_local std::mt19937_64 generator{std::random_device{}()};
    static constexpr std::array<char, 16> hex{'0', '1', '2', '3', '4', '5', '6', '7',
                                              '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string value(12, '0');
    for (char& digit : value) {
        digit = hex[generator() % hex.size()];
    }
    return value;
}

}  // namespace emerick::core
