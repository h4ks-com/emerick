#include "emerick/yamaha/sysex.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "emerick/core/midi.hpp"

namespace emerick::yamaha {

namespace {

using core::midi::kSysexEnd;
using core::midi::kSysexStart;

constexpr std::uint8_t kYamahaId = 0x43;
constexpr std::uint8_t kStyleGroup = 0x7E;
constexpr std::uint8_t kSectionControl = 0x00;
constexpr std::uint8_t kChordControl = 0x02;
constexpr std::uint8_t kNone = 0x7F;
constexpr std::uint8_t kOn = 0x7F;
constexpr std::size_t kChordLength = 9;
constexpr std::size_t kSectionLength = 7;

std::uint8_t requireCode(std::string_view name) {
    const auto code = noteCode(name);
    if (!code) {
        throw std::invalid_argument("invalid note name: " + std::string(name));
    }
    return *code;
}

bool hasHeader(std::span<const std::uint8_t> message, std::size_t length, std::uint8_t function) {
    return message.size() == length && message[0] == kSysexStart && message[1] == kYamahaId &&
           message[2] == kStyleGroup && message[3] == function && message[length - 1] == kSysexEnd;
}

std::optional<SectionPress> sectionFromCode(std::uint8_t code, bool on) {
    const int variation = code & 0x07;
    const auto* const found =
        std::ranges::find(kSections, std::optional<std::uint8_t>(code & 0xF8), &SectionInfo::sysexFamily);
    if (found == kSections.end() || variation >= variationCount(*found)) {
        return std::nullopt;
    }
    return SectionPress{found->kind, variation, on};
}

}  // namespace

std::optional<Chord> decodeChordControl(std::span<const std::uint8_t> message) {
    if (!hasHeader(message, kChordLength, kChordControl)) {
        return std::nullopt;
    }
    const auto root = noteFromCode(message[4]);
    const int type = message[5];
    if (!root || !isChordType(type)) {
        return std::nullopt;
    }
    Chord chord{*root, type, std::nullopt};
    const std::uint8_t bassCode = message[6];
    if (type == kNoChordType || bassCode == kNone || bassCode == message[4]) {
        return chord;
    }
    chord.bass = noteFromCode(bassCode);
    return chord.bass ? std::optional(chord) : std::nullopt;
}

std::optional<SectionPress> decodeSectionControl(std::span<const std::uint8_t> message) {
    if (!hasHeader(message, kSectionLength, kSectionControl)) {
        return std::nullopt;
    }
    return sectionFromCode(message[4], message[5] == kOn);
}

std::vector<std::uint8_t> encodeChordControl(const Chord& chord) {
    const auto root = requireCode(chord.root.name);
    const auto bass = chord.bass ? requireCode(chord.bass->name) : kNone;
    return {kSysexStart, kYamahaId, kStyleGroup, kChordControl, root, static_cast<std::uint8_t>(chord.type),
            bass,        kNone,     kSysexEnd};
}

std::vector<std::uint8_t> encodeSectionControl(const SectionPress& press) {
    const auto family = sectionInfo(press.kind).sysexFamily;
    if (!family) {
        throw std::invalid_argument("a Fill has no Section Control switch, send the Fill In of its Main");
    }
    return {kSysexStart,
            kYamahaId,
            kStyleGroup,
            kSectionControl,
            static_cast<std::uint8_t>(*family + press.variation),
            press.on ? kOn : std::uint8_t{0},
            kSysexEnd};
}

}  // namespace emerick::yamaha
