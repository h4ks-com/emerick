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
    [[nodiscard]] bool looped() const { return loopEnd > loopStart && loopEnd <= samples.size(); }
};

// Decodes a store wave blob: a FORM YTWV holding 16-bit linear audio in COMM and SSND chunks. We cannot decode the
// WXC1 compressed blobs yet, so those give nullopt.
std::optional<Wave> decodeWave(std::span<const std::uint8_t> blob);
std::optional<Wave> loadWave(const std::filesystem::path& path);

// True when the blob at this path holds audio decodeWave reads; it reads the chunk headers only.
bool isDecodableWave(const std::filesystem::path& path);

}  // namespace emerick::voices
