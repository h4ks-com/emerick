#pragma once

#include <cstdint>

namespace emerick::core::midi {

inline constexpr std::uint8_t kNoteOff = 0x80;
inline constexpr std::uint8_t kNoteOn = 0x90;
inline constexpr std::uint8_t kControlChange = 0xB0;
inline constexpr std::uint8_t kProgramChange = 0xC0;
inline constexpr std::uint8_t kPitchBend = 0xE0;
inline constexpr std::uint8_t kSysexStart = 0xF0;
inline constexpr std::uint8_t kSysexEnd = 0xF7;
inline constexpr std::uint8_t kClock = 0xF8;
inline constexpr std::uint8_t kStart = 0xFA;
inline constexpr std::uint8_t kStop = 0xFC;
inline constexpr std::uint8_t kBankSelectCc = 0;
inline constexpr std::uint8_t kBankSelectLsbCc = 32;
inline constexpr std::uint8_t kSustainCc = 64;
inline constexpr std::uint8_t kAllSoundOffCc = 120;
inline constexpr std::uint8_t kAllNotesOffCc = 123;

// Channels count from 1, as musicians and the Genos2 manuals write them.
constexpr std::uint8_t statusFor(std::uint8_t type, int channel) {
    return static_cast<std::uint8_t>(type | ((channel - 1) & 0x0F));
}

constexpr bool isChannelMessage(std::uint8_t status) { return status >= kNoteOff && status < kSysexStart; }

}  // namespace emerick::core::midi
