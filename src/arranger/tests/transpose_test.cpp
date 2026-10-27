#include "emerick/arranger/transpose.hpp"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string_view>
#include <vector>

#include "emerick/arranger/style.hpp"
#include "emerick/yamaha/chord.hpp"

using namespace emerick::arranger;
using emerick::yamaha::Chord;

namespace {

ChannelRule ruleWith(Transposition transposition, int sourceType = 2) {
    ChannelRule rule{.sourceType = sourceType};
    rule.ranges.fill(transposition);
    return rule;
}

Chord chord(std::string_view name) { return *Chord::parse(name); }

std::vector<int> transposeAll(const ChannelRule& rule, const std::vector<int>& notes, std::string_view name) {
    std::vector<int> result;
    for (const int note : notes) {
        result.push_back(transposeNote(rule, note, chord(name), false));
    }
    return result;
}

constexpr Transposition kMelody{.table = NoteTable::Melody};
constexpr Transposition kChord{.table = NoteTable::Chord};

}  // namespace

TEST_CASE("bypass with a fixed root leaves every note alone") {
    const auto rule = ruleWith({.rule = NoteRule::RootFixed, .table = NoteTable::Bypass});
    CHECK(transposeAll(rule, {36, 38, 42}, "F#m7") == std::vector{36, 38, 42});
}

TEST_CASE("bypass with root transposition moves by the root up to the high key") {
    const auto rule = ruleWith({.table = NoteTable::Bypass, .highKey = 5});
    CHECK(transposeAll(rule, {60, 64}, "F") == std::vector{65, 69});
    CHECK(transposeAll(rule, {60, 64}, "G") == std::vector{55, 59});
    CHECK(transposeAll(rule, {60, 64}, "Fm") == std::vector{65, 69});
}

TEST_CASE("the melody table keeps scale degrees on the new chord") {
    const auto rule = ruleWith(kMelody);
    CHECK(transposeAll(rule, {60, 62, 64, 67, 71}, "Dm7") == std::vector{62, 64, 65, 69, 72});
    CHECK(transposeAll(rule, {64, 71}, "C7") == std::vector{64, 70});
    CHECK(transposeAll(rule, {61, 63}, "Cm7") == std::vector{61, 63});
}

TEST_CASE("the chord table moves notes to tones of the new chord") {
    const auto rule = ruleWith({.table = NoteTable::Chord, .highKey = 4});
    CHECK(transposeAll(rule, {64, 67, 71}, "C") == std::vector{64, 67, 72});
    CHECK(transposeAll(rule, {64, 67, 71}, "Am") == std::vector{60, 64, 69});
    CHECK(transposeAll(rule, {64, 67, 71}, "Cm7") == std::vector{63, 67, 70});
    CHECK(transposeAll(rule, {64, 67, 71}, "C(1+8)") == std::vector{60, 72, 72});
}

TEST_CASE("the minor tables change only the degrees they name") {
    const std::vector<int> scale{64, 67, 69, 71};
    const auto table = [&](NoteTable value, std::string_view name) {
        return transposeAll(ruleWith({.table = value}, 0), scale, name);
    };
    CHECK(table(NoteTable::MelodicMinor, "Cm") == std::vector{63, 67, 69, 71});
    CHECK(table(NoteTable::HarmonicMinor, "Cm") == std::vector{63, 67, 68, 71});
    CHECK(table(NoteTable::NaturalMinor, "Cm") == std::vector{63, 67, 68, 70});
    CHECK(table(NoteTable::Dorian, "Cm") == std::vector{63, 67, 69, 70});
    CHECK(table(NoteTable::MelodicMinor, "C7") == scale);
    CHECK(table(NoteTable::MelodicMinor, "Cdim") == std::vector{63, 67, 69, 71});
    CHECK(table(NoteTable::MelodicMinorFifth, "Cdim") == std::vector{63, 66, 69, 71});
    CHECK(table(NoteTable::HarmonicMinorFifth, "Caug") == std::vector{64, 68, 69, 71});
    CHECK(transposeAll(ruleWith({.table = NoteTable::HarmonicMinor}, 8), {63, 68}, "C") == std::vector{64, 69});
}

TEST_CASE("a fixed root keeps each note as close to where the style wrote it") {
    const auto rule = ruleWith({.rule = NoteRule::RootFixed, .table = NoteTable::Melody});
    CHECK(transposeAll(rule, {64, 67, 71}, "F") == std::vector{69, 72, 76});
    CHECK(transposeAll(rule, {64, 67, 71}, "C") == std::vector{64, 67, 71});
}

TEST_CASE("note limits fold transposed notes into their range by octaves") {
    const auto rule = ruleWith({.table = NoteTable::Melody, .noteLow = 48, .noteHigh = 60});
    CHECK(transposeAll(rule, {40, 70, 55}, "C") == std::vector{52, 58, 55});
    CHECK(transposeAll(rule, {59}, "D") == std::vector{49});
}

TEST_CASE("a part with bass on plays the bass note of a slash chord for the root") {
    const auto bass = ruleWith({.table = NoteTable::Melody, .bassOn = true});
    CHECK(transposeAll(bass, {36, 43, 40}, "C/E") == std::vector{40, 43, 40});
    CHECK(transposeAll(bass, {36, 43}, "F/A") == std::vector{45, 48});
    CHECK(transposeAll(ruleWith(kMelody), {36}, "C/E") == std::vector{36});
}

TEST_CASE("an SFF2 rule picks the range of each source note") {
    ChannelRule rule{.middleLow = 50, .middleHigh = 80};
    rule.ranges = {Transposition{.rule = NoteRule::RootFixed, .table = NoteTable::Bypass}, kMelody, kChord};
    CHECK(transposeAll(rule, {40, 64, 88}, "D") == std::vector{40, 66, 90});
    CHECK(&transpositionFor(rule, 50) == &rule.ranges[1]);
    CHECK(&transpositionFor(rule, 81) == &rule.ranges[2]);
}

TEST_CASE("a MegaVoice part keeps its noise notes where the style wrote them") {
    const auto rule = ruleWith(kMelody);
    CHECK(transposeNote(rule, 90, chord("F"), true) == 90);
    CHECK(transposeNote(rule, 90, chord("F"), false) == 95);
    CHECK(transposeNote(rule, 64, chord("F"), true) == 69);
}

TEST_CASE("no chord leaves notes as written") {
    CHECK(transposeAll(ruleWith(kChord), {64, 67, 71}, "N.C.") == std::vector{64, 67, 71});
}

TEST_CASE("note and chord mutes silence a channel on those chords") {
    ChannelRule rule;
    rule.playsOnRoot = static_cast<std::uint16_t>(0x0FFF & ~(1U << 2));
    rule.playsOnType &= ~(std::uint64_t{1} << 10);
    CHECK_FALSE(playsOn(rule, chord("D")));
    CHECK_FALSE(playsOn(rule, chord("Em7")));
    CHECK(playsOn(rule, chord("E")));
    CHECK(playsOn(rule, chord("N.C.")));
    rule.playsOnType &= ~(std::uint64_t{1} << emerick::yamaha::kNoChordType);
    CHECK_FALSE(playsOn(rule, chord("N.C.")));
}

TEST_CASE("the retrigger rule decides what a held note does on a chord change") {
    const auto with = [](RetriggerRule retrigger) {
        return ruleWith({.table = NoteTable::Melody, .bassOn = true, .retrigger = retrigger});
    };
    CHECK(retriggerNote(with(RetriggerRule::Stop), 36, 36, chord("F"), false) == std::nullopt);
    CHECK(retriggerNote(with(RetriggerRule::Retrigger), 40, 40, chord("F"), false) == 45);
    CHECK(retriggerNote(with(RetriggerRule::PitchShift), 40, 40, chord("Fm"), false) == 44);
    CHECK(retriggerNote(with(RetriggerRule::RetriggerToRoot), 40, 43, chord("F"), false) == 41);
    CHECK(retriggerNote(with(RetriggerRule::PitchShiftToRoot), 40, 43, chord("C/E"), false) == 40);
}
