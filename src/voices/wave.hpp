#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace emerick::voices {

// A decoded mono wave and its sustain loop, in frames; the loop runs from loopStart up to loopEnd.
struct Wave {
    std::vector<float> samples;
    std::uint32_t loopStart = 0;
    std::uint32_t loopEnd = 0;
    // Set when the WXC1 predictor ran away, pushing more samples past full scale than real audio does.
    bool noisy = false;
    // Set when a one-shot WXC1 wave touches full scale and grows louder after its attack at a low pitch, as a
    // predictor drifting into a drone does. Slow melodic attacks swell too, so we count it as noise in drum kits only.
    bool swells = false;
    [[nodiscard]] bool looped() const { return loopEnd > loopStart && loopEnd <= samples.size(); }
};

// Decodes a store wave blob: a FORM YTWV holding 16-bit linear audio in COMM and SSND chunks, or WXC1 compressed audio.
std::optional<Wave> decodeWave(std::span<const std::uint8_t> blob);
std::optional<Wave> loadWave(const std::filesystem::path& path);

// True when the blob at this path holds audio decodeWave reads; it reads the chunk headers only.
bool isDecodableWave(const std::filesystem::path& path);

}  // namespace emerick::voices
