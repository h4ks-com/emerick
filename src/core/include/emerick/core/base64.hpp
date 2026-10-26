#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace emerick::core {

// Standard base64 with padding, as RFC 4648 writes it.
std::string toBase64(std::span<const std::uint8_t> bytes);
// Nullopt for text that is not padded base64.
std::optional<std::vector<std::uint8_t>> fromBase64(std::string_view text);

}  // namespace emerick::core
