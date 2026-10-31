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
// A hidden read-only param whose label names the notes of the voice whose waves decode with noise, which hosts show as
// a warning; it is empty for a clean voice.
inline constexpr std::uint32_t kNoiseParam = 6;
inline constexpr std::uint32_t kNoisySamplesParam = 7;
inline constexpr std::uint32_t kParamCount = 8;

// The Voice param's label for no voice while the store loads, which hosts read to wait for the store.
inline constexpr std::string_view kLoadingLabel = "Loading voices";

// The environment variable naming the store folder of every instance whose state names none, which the host sets.
inline constexpr const char* kFolderVariable = "EMERICK_VOICES_FOLDER";

}  // namespace emerick::voices
