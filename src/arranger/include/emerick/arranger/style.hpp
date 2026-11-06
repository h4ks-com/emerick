#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "emerick/core/time.hpp"
#include "emerick/yamaha/section.hpp"

namespace emerick::arranger {

inline constexpr int kChannels = 16;
inline constexpr int kParts = 8;
inline constexpr int kFirstPartChannel = 8;
// Chord mute bits cover every chord type and N.C., which sits at bit yamaha::kNoChordType.
inline constexpr int kChordMuteBits = 35;

// NTR: how a part follows the chord root.
enum class NoteRule : std::uint8_t { RootTranspose, RootFixed, Guitar };

// NTT: how a part follows the chord type. The Guitar tables apply to the Guitar rule only.
enum class NoteTable : std::uint8_t {
    Bypass,
    Melody,
    Chord,
    MelodicMinor,
    MelodicMinorFifth,
    HarmonicMinor,
    HarmonicMinorFifth,
    NaturalMinor,
    NaturalMinorFifth,
    Dorian,
    DorianFifth,
    GuitarAllPurpose,
    GuitarStroke,
    GuitarArpeggio,
};

// RTR: what a held note does when the chord changes under it.
enum class RetriggerRule : std::uint8_t {
    Stop,
    PitchShift,
    PitchShiftToRoot,
    Retrigger,
    RetriggerToRoot,
    NoteGenerator
};

struct Transposition {
    NoteRule rule = NoteRule::RootTranspose;
    NoteTable table = NoteTable::Melody;
    bool bassOn = false;
    int highKey = 11;
    int noteLow = 0;
    int noteHigh = 127;
    RetriggerRule retrigger = RetriggerRule::Retrigger;
    bool operator==(const Transposition&) const = default;
};

// One CASM channel table: where a source channel plays and how it follows the chord. An SFF1 table has one
// transposition; an SFF2 table has one for the source notes below middleLow, between the limits and above middleHigh.
struct ChannelRule {
    int destination = kFirstPartChannel;
    std::uint16_t playsOnRoot = 0x0FFF;
    std::uint64_t playsOnType = (std::uint64_t{1} << kChordMuteBits) - 1;
    int sourceRoot = 0;
    int sourceType = 2;
    int middleLow = 0;
    int middleHigh = 127;
    std::array<Transposition, 3> ranges{};
    bool operator==(const ChannelRule&) const = default;
};

// A channel message of a section, its tick counted in the style's own ticks from the section start.
struct StyleEvent {
    std::int64_t tick = 0;
    std::array<std::uint8_t, 3> bytes{};
    bool operator==(const StyleEvent&) const = default;
};

struct StyleSection {
    yamaha::Section kind = yamaha::Section::Main;
    int variation = 0;
    std::int64_t length = 0;
    std::vector<StyleEvent> events;
    // By source channel; a channel without a rule is not part of the style.
    std::array<std::optional<ChannelRule>, kChannels> rules{};
};

struct PartVoice {
    std::uint8_t bankMsb = 0;
    std::uint8_t bankLsb = 0;
    std::uint8_t program = 0;
    // Every control change and program change of the style's setup block on the part's source channel, in order.
    std::vector<std::array<std::uint8_t, 3>> setup;
    bool operator==(const PartVoice&) const = default;
};

struct Style {
    int ticksPerQuarter = 480;
    double bpm = 120.0;
    core::Meter meter{};
    std::vector<StyleSection> sections;
    // By part, Rhythm 1 to Phrase 2 on channels 9 to 16.
    std::array<std::optional<PartVoice>, kParts> voices{};
};

class StyleError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Reads an SFF1 or SFF2 style (.sty, .prs, .sst, .bst) and throws StyleError on anything malformed.
Style parseStyle(std::span<const std::uint8_t> bytes);

const StyleSection* findSection(const Style& style, yamaha::Section kind, int variation);
// The section a song asking for kind and variation plays. A song that asks for an Intro or Ending the style lacks
// plays the style's nearest variation of it, so it still starts and ends.
const StyleSection* requestedSection(const Style& style, yamaha::Section kind, int variation);

// How many times each note starts in a section, by part, as the pattern writes it before any chord transposes it.
std::array<std::map<int, int>, kParts> partNotes(const StyleSection& section);

// FNV-1a of a style file's bytes in hex, so a project notices a style file that changed.
std::string styleHash(std::span<const std::uint8_t> bytes);

// The Arranger keeps a style up to this size in its state, so a project file alone plays it.
inline constexpr std::size_t kEmbeddedStyleLimit = std::size_t{2} * 1024 * 1024;

}  // namespace emerick::arranger
