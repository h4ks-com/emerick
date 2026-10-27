#include "casm.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "emerick/arranger/style.hpp"
#include "emerick/yamaha/chord.hpp"
#include "emerick/yamaha/section.hpp"
#include "reader.hpp"

namespace emerick::arranger {

namespace {

using yamaha::Section;

struct NamedSection {
    std::string_view text;
    SectionName section;
};

// Each style fill leads into the Main it is named after, so we keep it as that Main's FillTo. Fill In BA is the
// Break of SFF2 styles.
constexpr auto kSectionNames = std::to_array<NamedSection>({
    {"Main A", {Section::Main, 0}},
    {"Main B", {Section::Main, 1}},
    {"Main C", {Section::Main, 2}},
    {"Main D", {Section::Main, 3}},
    {"Fill In AA", {Section::FillTo, 0}},
    {"Fill In BB", {Section::FillTo, 1}},
    {"Fill In CC", {Section::FillTo, 2}},
    {"Fill In DD", {Section::FillTo, 3}},
    {"Fill In BA", {Section::Break, 0}},
    {"Break", {Section::Break, 0}},
    {"Intro A", {Section::Intro, 0}},
    {"Intro B", {Section::Intro, 1}},
    {"Intro C", {Section::Intro, 2}},
    {"Ending A", {Section::Ending, 0}},
    {"Ending B", {Section::Ending, 1}},
    {"Ending C", {Section::Ending, 2}},
});

constexpr std::size_t kCtabLength = 27;
constexpr std::size_t kCtb2Length = 47;
constexpr std::size_t kCnttLength = 2;
constexpr int kPitchClasses = 12;
constexpr std::uint8_t kBassOnBit = 0x80;
constexpr int kRhythm2Channel = 9;
constexpr int kBassChannel = 10;
constexpr int kPadChannel = 13;

enum class Dialect : std::uint8_t { Sff1, Sff2 };

constexpr std::array kSff1Tables{NoteTable::Bypass, NoteTable::Melody,       NoteTable::Chord,
                                 NoteTable::Melody, NoteTable::MelodicMinor, NoteTable::HarmonicMinor};
constexpr int kSff1BassTable = 3;
constexpr std::array kSff2Tables{NoteTable::Bypass,
                                 NoteTable::Melody,
                                 NoteTable::Chord,
                                 NoteTable::MelodicMinor,
                                 NoteTable::MelodicMinorFifth,
                                 NoteTable::HarmonicMinor,
                                 NoteTable::HarmonicMinorFifth,
                                 NoteTable::NaturalMinor,
                                 NoteTable::NaturalMinorFifth,
                                 NoteTable::Dorian,
                                 NoteTable::DorianFifth};
constexpr std::array kGuitarTables{NoteTable::GuitarAllPurpose, NoteTable::GuitarStroke, NoteTable::GuitarArpeggio};

template <typename Enum>
Enum enumValue(std::uint8_t value, Enum last, const char* message) {
    if (value > static_cast<std::uint8_t>(last)) {
        throw StyleError(message);
    }
    return static_cast<Enum>(value);
}

// Factory styles set the top bit of some note limits, so we read only the low seven bits as the note.
int midiValue(std::uint8_t value) { return value & 0x7F; }

template <std::size_t Size>
NoteTable tableFrom(const std::array<NoteTable, Size>& tables, int value) {
    if (std::cmp_greater_equal(value, Size)) {
        throw StyleError("The style has a CASM NTT that no keyboard knows.");
    }
    return tables.at(static_cast<std::size_t>(value));
}

// We read the six bytes NTR, NTT, high key, note low limit, note high limit and RTR.
Transposition readTransposition(std::span<const std::uint8_t> bytes, Dialect dialect) {
    Transposition result;
    result.rule = enumValue(bytes[0], NoteRule::Guitar, "The style has a CASM NTR that no keyboard knows.");
    const int table = bytes[1] & ~kBassOnBit;
    result.bassOn = (bytes[1] & kBassOnBit) != 0;
    if (result.rule == NoteRule::Guitar) {
        result.table = tableFrom(kGuitarTables, table);
    } else if (dialect == Dialect::Sff1) {
        result.table = tableFrom(kSff1Tables, table);
        result.bassOn = result.bassOn || table == kSff1BassTable;
    } else {
        result.table = tableFrom(kSff2Tables, table);
    }
    result.highKey = midiValue(bytes[2]);
    result.noteLow = midiValue(bytes[3]);
    result.noteHigh = midiValue(bytes[4]);
    result.retrigger =
        enumValue(bytes[5], RetriggerRule::NoteGenerator, "The style has a CASM RTR that no keyboard knows.");
    return result;
}

int channelValue(std::uint8_t value) {
    if (value >= kChannels) {
        throw StyleError("The style has a CASM channel above 16.");
    }
    return value;
}

struct SourceRule {
    int channel = 0;
    ChannelRule rule;
};

SourceRule readTable(std::span<const std::uint8_t> bytes, Dialect dialect) {
    const std::size_t needed = dialect == Dialect::Sff1 ? kCtabLength : kCtb2Length;
    if (bytes.size() < needed) {
        throw StyleError("The style has a CASM channel table that is too short.");
    }
    SourceRule result{.channel = channelValue(bytes[0]), .rule = {}};
    auto& rule = result.rule;
    rule.destination = channelValue(bytes[9]);
    rule.playsOnRoot = static_cast<std::uint16_t>(((bytes[11] << 8) | bytes[12]) & 0x0FFF);
    rule.playsOnType = 0;
    for (const std::uint8_t part : bytes.subspan(13, 5)) {
        rule.playsOnType = (rule.playsOnType << 8) | part;
    }
    rule.playsOnType &= (std::uint64_t{1} << kChordMuteBits) - 1;
    if (bytes[18] >= kPitchClasses || bytes[19] >= yamaha::kNoChordType) {
        throw StyleError("The style has a CASM source chord that no keyboard knows.");
    }
    rule.sourceRoot = bytes[18];
    rule.sourceType = bytes[19];
    if (dialect == Dialect::Sff1) {
        rule.ranges.fill(readTransposition(bytes.subspan(20, 6), dialect));
        return result;
    }
    constexpr std::size_t kRangeLength = 6;
    rule.middleLow = midiValue(bytes[20]);
    rule.middleHigh = midiValue(bytes[21]);
    for (std::size_t range = 0; range < rule.ranges.size(); ++range) {
        rule.ranges.at(range) = readTransposition(bytes.subspan(22 + range * kRangeLength, kRangeLength), dialect);
    }
    return result;
}

std::vector<SectionName> sectionList(std::span<const std::uint8_t> bytes) {
    std::vector<SectionName> result;
    const std::string text = textOf(bytes);
    for (const auto part : std::views::split(text, ',')) {
        std::string_view name(part.begin(), part.end());
        name.remove_prefix(std::min(name.find_first_not_of(' '), name.size()));
        if (const auto section = sectionName(name)) {
            result.push_back(*section);
        }
    }
    return result;
}

void readSegment(std::span<const std::uint8_t> body, Style& style) {
    std::vector<SectionName> names;
    std::vector<SourceRule> rules;
    for (const auto& chunk : chunks(body)) {
        if (chunk.id == fourCc("Sdec")) {
            names = sectionList(chunk.body);
        } else if (chunk.id == fourCc("Ctab")) {
            rules.push_back(readTable(chunk.body, Dialect::Sff1));
        } else if (chunk.id == fourCc("Ctb2")) {
            rules.push_back(readTable(chunk.body, Dialect::Sff2));
        } else if (chunk.id == fourCc("Cntt")) {
            if (chunk.body.size() < kCnttLength) {
                throw StyleError("The style has a CASM Cntt that is too short.");
            }
            const int channel = channelValue(chunk.body[0]);
            const bool bassOn = (chunk.body[1] & kBassOnBit) != 0;
            for (auto& source :
                 rules | std::views::filter([&](const SourceRule& rule) { return rule.channel == channel; })) {
                std::ranges::for_each(source.rule.ranges, [&](Transposition& range) { range.bassOn = bassOn; });
            }
        }
    }
    for (auto& section : style.sections) {
        const bool named = std::ranges::any_of(names, [&](const SectionName& name) {
            return name.kind == section.kind && name.variation == section.variation;
        });
        if (!named) {
            continue;
        }
        for (const auto& source : rules) {
            section.rules.at(static_cast<std::size_t>(source.channel)) = source.rule;
        }
    }
}

}  // namespace

std::optional<SectionName> sectionName(std::string_view text) {
    const auto* const found = std::ranges::find(kSectionNames, text, &NamedSection::text);
    return found == kSectionNames.end() ? std::nullopt : std::optional{found->section};
}

ChannelRule defaultRule(int sourceChannel) {
    ChannelRule rule{.destination = sourceChannel};
    Transposition& transposition = rule.ranges[0];
    if (sourceChannel <= kRhythm2Channel) {
        transposition = {.rule = NoteRule::RootFixed, .table = NoteTable::Bypass, .retrigger = RetriggerRule::Stop};
    } else if (sourceChannel == kBassChannel) {
        transposition = {.table = NoteTable::Melody,
                         .bassOn = true,
                         .highKey = 3,
                         .noteLow = 28,
                         .retrigger = RetriggerRule::RetriggerToRoot};
    } else if (sourceChannel <= kPadChannel) {
        transposition = {.table = NoteTable::Chord, .highKey = 7, .retrigger = RetriggerRule::PitchShift};
    } else {
        transposition = {.table = NoteTable::Melody, .highKey = 7, .retrigger = RetriggerRule::PitchShift};
    }
    rule.ranges.fill(transposition);
    if (sourceChannel > kRhythm2Channel) {
        rule.playsOnType &= ~(std::uint64_t{1} << yamaha::kNoChordType);
    }
    return rule;
}

void readCasm(std::span<const std::uint8_t> body, Style& style) {
    for (const auto& chunk : chunks(body)) {
        if (chunk.id == fourCc("CSEG")) {
            readSegment(chunk.body, style);
        }
    }
}

}  // namespace emerick::arranger
