#pragma once

#include <cstdint>

namespace emerick::sampler {

// A hidden read-only param whose label says why the instrument plays nothing, which hosts show as a warning; it is
// empty while the instrument plays.
inline constexpr std::uint32_t kProblemParam = 0;
// A hidden read-only param that reads 1 while the instrument loads, which hosts read to wait for it.
inline constexpr std::uint32_t kLoadingParam = 1;
inline constexpr std::uint32_t kParamCount = 2;

}  // namespace emerick::sampler
