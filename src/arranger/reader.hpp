#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/arranger/style.hpp"

namespace emerick::arranger {

using FourCc = std::array<std::uint8_t, 4>;

constexpr FourCc fourCc(std::string_view text) {
    return {static_cast<std::uint8_t>(text[0]), static_cast<std::uint8_t>(text[1]), static_cast<std::uint8_t>(text[2]),
            static_cast<std::uint8_t>(text[3])};
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool atEnd() const { return position_ == bytes_.size(); }
    [[nodiscard]] std::size_t remaining() const { return bytes_.size() - position_; }

    std::uint8_t peek() const {
        need(1);
        return bytes_[position_];
    }

    std::uint8_t byte() {
        need(1);
        return bytes_[position_++];
    }

    std::uint8_t dataByte() {
        const std::uint8_t value = byte();
        if (value >= 0x80) {
            throw StyleError("The style has a MIDI message cut short by a status byte.");
        }
        return value;
    }

    std::uint32_t u32() {
        std::uint32_t value = 0;
        for (const std::uint8_t part : take(4)) {
            value = (value << 8) | part;
        }
        return value;
    }

    std::uint32_t vlq() {
        constexpr int kMaxVlqBytes = 4;
        std::uint32_t value = 0;
        for (int count = 0; count < kMaxVlqBytes; ++count) {
            const std::uint8_t part = byte();
            value = (value << 7) | (part & 0x7FU);
            if ((part & 0x80) == 0) {
                return value;
            }
        }
        throw StyleError("The style has a MIDI number longer than four bytes.");
    }

    std::span<const std::uint8_t> take(std::size_t count) {
        need(count);
        const auto part = bytes_.subspan(position_, count);
        position_ += count;
        return part;
    }

private:
    void need(std::size_t count) const {
        if (count > remaining()) {
            throw StyleError("The style ends in the middle of its data.");
        }
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
};

// Text in styles is ASCII padded with spaces or zeros, which we drop.
inline std::string textOf(std::span<const std::uint8_t> bytes) {
    std::string text(bytes.size(), ' ');
    std::ranges::transform(bytes, text.begin(), [](std::uint8_t byte) { return static_cast<char>(byte); });
    const auto last = text.find_last_not_of(std::string_view(" \0", 2));
    text.resize(last == std::string::npos ? 0 : last + 1);
    return text;
}

struct Chunk {
    FourCc id{};
    std::span<const std::uint8_t> body;
};

inline std::vector<Chunk> chunks(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kChunkHeader = 8;
    std::vector<Chunk> result;
    Reader reader(bytes);
    while (reader.remaining() >= kChunkHeader) {
        Chunk chunk;
        std::ranges::copy(reader.take(4), chunk.id.begin());
        chunk.body = reader.take(reader.u32());
        result.push_back(chunk);
    }
    return result;
}

}  // namespace emerick::arranger
