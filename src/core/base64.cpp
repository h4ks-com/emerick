#include "emerick/core/base64.hpp"

#include <algorithm>
#include <array>

namespace emerick::core {

namespace {

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::uint8_t kInvalid = 0xFF;

constexpr std::array<std::uint8_t, 256> kValues = [] {
    std::array<std::uint8_t, 256> values{};
    values.fill(kInvalid);
    for (std::size_t index = 0; index < kAlphabet.size(); ++index) {
        values[static_cast<unsigned char>(kAlphabet[index])] = static_cast<std::uint8_t>(index);
    }
    return values;
}();

}  // namespace

std::string toBase64(std::span<const std::uint8_t> bytes) {
    std::string text;
    text.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t at = 0; at < bytes.size(); at += 3) {
        const std::size_t count = std::min<std::size_t>(3, bytes.size() - at);
        std::uint32_t group = std::uint32_t{bytes[at]} << 16U;
        if (count > 1) {
            group |= std::uint32_t{bytes[at + 1]} << 8U;
        }
        if (count > 2) {
            group |= bytes[at + 2];
        }
        for (std::size_t digit = 0; digit < 4; ++digit) {
            text += digit <= count ? kAlphabet[(group >> (18U - (6U * digit))) & 0x3FU] : '=';
        }
    }
    return text;
}

std::optional<std::vector<std::uint8_t>> fromBase64(std::string_view text) {
    if (text.size() % 4 != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size() / 4 * 3);
    for (std::size_t at = 0; at < text.size(); at += 4) {
        const bool last = at + 4 == text.size();
        const auto padding = last ? static_cast<std::size_t>(std::ranges::count(text.substr(at + 2, 2), '=')) : 0;
        std::uint32_t group = 0;
        for (std::size_t digit = 0; digit < 4; ++digit) {
            const std::uint8_t value = digit >= 4 - padding ? 0 : kValues[static_cast<unsigned char>(text[at + digit])];
            if (value == kInvalid) {
                return std::nullopt;
            }
            group = (group << 6U) | value;
        }
        for (std::size_t index = 0; index < 3 - padding; ++index) {
            bytes.push_back(static_cast<std::uint8_t>(group >> (16U - (8U * index))));
        }
    }
    return bytes;
}

}  // namespace emerick::core
