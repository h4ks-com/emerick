#pragma once

#include <array>

#include "emerick/app/view_actions.hpp"

namespace emerick::app {

inline constexpr ActionKeys kSelectTrackKeys{"track.select", "Select track", "", ""};
inline constexpr ActionKeys kMuteKeys{"track.setMute", "Mute track", "M", ""};
inline constexpr ActionKeys kSoloKeys{"track.setSolo", "Solo track", "S", ""};
inline constexpr ActionKeys kTrackGainKeys{"track.setGain", "Set track gain", "", ""};
inline constexpr ActionKeys kAutomationModeKeys{"automation.setMode", "Set automation mode", "", ""};
inline constexpr ActionKeys kTempoKeys{"project.setTempo", "Set tempo and meter", "", ""};
inline constexpr ActionKeys kSendLevelKeys{"send.setLevel", "Set send level", "", ""};
inline constexpr ActionKeys kSendPreKeys{"send.setPre", "Set send pre-fader", "", ""};

// The read-only viewer shows these controls turned off and names them in their tooltips as the app does.
inline constexpr std::array kReadOnlyActions{kSelectTrackKeys,    kMuteKeys,  kSoloKeys,      kTrackGainKeys,
                                             kAutomationModeKeys, kTempoKeys, kSendLevelKeys, kSendPreKeys};

}  // namespace emerick::app
