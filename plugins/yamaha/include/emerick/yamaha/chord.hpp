#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace emerick::yamaha {

inline constexpr int kNoChordType = 0x22;
inline constexpr std::size_t kMaxChordIntervals = 5;

struct ChordType {
    constexpr ChordType(std::string_view typeName, std::initializer_list<int> typeIntervals)
        : name(typeName), count(typeIntervals.size()) {
        std::ranges::copy(typeIntervals, slots.begin());
    }

    [[nodiscard]] constexpr std::span<const int> intervals() const { return std::span(slots).first(count); }

    std::string_view name;
    std::array<int, kMaxChordIntervals> slots{};
    std::size_t count = 0;
};

bool isChordType(int type);
const ChordType& chordType(int type);

struct Note {
    int pitchClass = 0;
    std::string name;
    bool operator==(const Note&) const = default;
};

// A note code has the accidental in its high nibble (3 is natural) and the letter C to B as 1 to 7 in the low nibble.
std::optional<Note> noteFromCode(std::uint8_t code);
std::optional<std::uint8_t> noteCode(std::string_view name);

struct Chord {
    Note root{0, "C"};
    int type = 0;
    std::optional<Note> bass;

    [[nodiscard]] bool isNoChord() const { return type == kNoChordType; }
    [[nodiscard]] std::string name() const;
    static std::optional<Chord> parse(std::string_view text);
    // Block voicing: root and intervals above baseOctaveNote, the bass note an octave below.
    [[nodiscard]] std::vector<int> notes(int baseOctaveNote) const;
    bool operator==(const Chord&) const = default;
};

}  // namespace emerick::yamaha
