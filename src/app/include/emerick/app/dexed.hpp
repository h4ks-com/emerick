#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace emerick::app {

inline constexpr const char* kDexedPluginId = "com.digital-suburban.dexed";
inline constexpr std::size_t kDx7Programs = 32;

// A DX7 32-voice bulk dump, the 4104 bytes of its sysex.
struct Dx7Cartridge {
    std::vector<std::uint8_t> sysex;
};

// The first 32-voice bulk dump in the bytes of a .syx file.
[[nodiscard]] std::optional<Dx7Cartridge> parseDx7Cartridge(std::span<const std::uint8_t> bytes);

// The name of each of the cartridge's 32 programs, as Dexed shows it, without trailing spaces.
[[nodiscard]] std::vector<std::string> dx7ProgramNames(const Dx7Cartridge& cartridge);

// Dexed's saved state: JUCE's binary XML, which holds the cartridge, the program it plays, unpacked, and its number.
struct DexedState {
    std::string xml;
    Dx7Cartridge cartridge;
    std::vector<std::uint8_t> voice;
    std::size_t program = 0;
};

[[nodiscard]] std::optional<DexedState> parseDexedState(std::span<const std::uint8_t> bytes);

// The state with the cartridge loaded and its program, from 0, picked, as Dexed saves it after a program change.
[[nodiscard]] std::vector<std::uint8_t> dexedStateWith(const DexedState& state, const Dx7Cartridge& cartridge,
                                                       std::size_t program);

}  // namespace emerick::app
