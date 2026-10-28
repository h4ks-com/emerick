#pragma once

#include <cstdint>
#include <string_view>

namespace emerick::voices {

inline constexpr std::uint32_t kVoiceParam = 0;
inline constexpr std::uint32_t kVolumeParam = 1;
inline constexpr std::uint32_t kTuneParam = 2;
inline constexpr std::uint32_t kAttackParam = 3;
inline constexpr std::uint32_t kReleaseParam = 4;
inline constexpr std::uint32_t kFollowParam = 5;
inline constexpr std::uint32_t kParamCount = 6;

// The Voice param's label for no voice while the store loads, which hosts read to wait for the store.
inline constexpr std::string_view kLoadingLabel = "Loading voices";

}  // namespace emerick::voices
