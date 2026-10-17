#pragma once

namespace emerick::core {

template <class... Handlers>
struct Overloaded : Handlers... {
    using Handlers::operator()...;
};

}  // namespace emerick::core
