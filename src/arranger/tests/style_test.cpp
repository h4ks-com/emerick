#include "emerick/arranger/style.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>

#include "emerick/yamaha/section.hpp"
#include "style_builder.hpp"

using namespace emerick::arranger;
using namespace emerick::arranger::test;
using emerick::yamaha::Section;

namespace {

int notesOn(const StyleSection& section, int channel) {
    return static_cast<int>(std::ranges::count_if(section.events, [&](const StyleEvent& event) {
        return event.bytes[0] == (0x90 | channel) && event.bytes[2] > 0;
    }));
}

void requireStyleError(const Bytes& bytes) { REQUIRE_THROWS_AS(parseStyle(bytes), StyleError); }

Bytes styleWith(const Bytes& casmChunk) {
    StyleBuilder builder;
    builder.marker(0, "SFF2");
    builder.marker(StyleBuilder::kBar, "Main A");
    builder.note(10, 36, 100, StyleBuilder::kBar, 100);
    builder.marker(StyleBuilder::kBar * 2, "Main B");
    builder.note(10, 38, 100, StyleBuilder::kBar * 2, 100);
    return concat(builder.smf(StyleBuilder::kBar * 3), casmChunk);
}

}  // namespace

TEST_CASE("a synthetic style round trips through the parser") {
    const Style style = parseStyle(demoStyle());
    CHECK(style.ticksPerQuarter == StyleBuilder::kTicksPerQuarter);
    CHECK(style.bpm == Catch::Approx(100.0));
    CHECK(style.meter == emerick::core::Meter{4, 4});
    REQUIRE(style.sections.size() == 7);

    const auto* mainA = findSection(style, Section::Main, 0);
    REQUIRE(mainA != nullptr);
    CHECK(mainA->length == 2 * StyleBuilder::kBar);
    CHECK(notesOn(*mainA, kDrums) == 8);
    CHECK(notesOn(*mainA, kChords) == 6);
    CHECK(mainA->events.front().tick == 0);

    CHECK(findSection(style, Section::FillTo, 0)->length == StyleBuilder::kBar);
    CHECK(findSection(style, Section::FillTo, 1) != nullptr);
    CHECK(findSection(style, Section::Break, 0) != nullptr);
    CHECK(findSection(style, Section::Intro, 0) != nullptr);
    CHECK(findSection(style, Section::Ending, 0) != nullptr);
    CHECK(findSection(style, Section::Main, 2) == nullptr);

    CHECK(style.voices[0] == PartVoice{127, 0, 0, 110, 64});
    CHECK(style.voices[2] == PartVoice{0, 0, 33, 100, 60});
    CHECK(style.voices[3] == PartVoice{8, 2, 25, 90, 70});
    CHECK_FALSE(style.voices[1].has_value());
}

TEST_CASE("an SFF1 table maps a source channel and reads its rule") {
    const Style style = parseStyle(demoStyle());
    const auto& bass = findSection(style, Section::Main, 0)->rules[kBass];
    REQUIRE(bass);
    CHECK(bass->destination == kBass);
    CHECK(bass->sourceType == 2);
    for (const auto& range : bass->ranges) {
        CHECK(range == Transposition{.rule = NoteRule::RootTranspose,
                                     .table = NoteTable::Melody,
                                     .bassOn = true,
                                     .highKey = 7,
                                     .noteLow = 28,
                                     .noteHigh = 60,
                                     .retrigger = RetriggerRule::RetriggerToRoot});
    }
}

TEST_CASE("each CSEG gives its tables to the sections it names") {
    const TableSpec remap{.source = 11,
                          .destination = 12,
                          .ranges = {{{0, 2, 7, 0, 127, 1}, {0, 2, 7, 0, 127, 1}, {0, 2, 7, 0, 127, 1}}}};
    const TableSpec keep{.source = 11,
                         .destination = 11,
                         .ranges = {{{1, 1, 5, 0, 127, 3}, {1, 1, 5, 0, 127, 3}, {1, 1, 5, 0, 127, 3}}}};
    const Style style = parseStyle(styleWith(casm({{"Main A", {ctab(remap)}}, {"Main B", {ctab(keep)}}})));
    CHECK(findSection(style, Section::Main, 0)->rules[11]->destination == 12);
    const auto& mainB = findSection(style, Section::Main, 1)->rules[11];
    CHECK(mainB->destination == 11);
    CHECK(mainB->ranges[0].rule == NoteRule::RootFixed);
    CHECK(mainB->ranges[0].retrigger == RetriggerRule::Retrigger);
}

TEST_CASE("an SFF2 table reads three ranges, the SFF2 tables and the bass bit") {
    const TableSpec spec{.source = 10,
                         .destination = 10,
                         .roots = 0x0FFE,
                         .types = 0x3FFFFFFFE,
                         .sourceRoot = 2,
                         .sourceType = 8,
                         .middleLow = 40,
                         .middleHigh = 90,
                         .ranges = {{{0, 0x81, 3, 28, 127, 4}, {1, 9, 7, 0, 127, 1}, {2, 1, 7, 0, 127, 0}}}};
    const Style style = parseStyle(styleWith(casm({{"Main A,Main B", {ctb2(spec)}}})));
    const auto& rule = findSection(style, Section::Main, 1)->rules[10];
    REQUIRE(rule);
    CHECK(rule->playsOnRoot == 0x0FFE);
    CHECK(rule->playsOnType == 0x3FFFFFFFE);
    CHECK(rule->sourceRoot == 2);
    CHECK(rule->sourceType == 8);
    CHECK(rule->middleLow == 40);
    CHECK(rule->middleHigh == 90);
    CHECK(rule->ranges[0].table == NoteTable::Melody);
    CHECK(rule->ranges[0].bassOn);
    CHECK(rule->ranges[1].table == NoteTable::Dorian);
    CHECK(rule->ranges[1].rule == NoteRule::RootFixed);
    CHECK(rule->ranges[2].table == NoteTable::GuitarStroke);
}

TEST_CASE("a Cntt chunk sets the bass flag of its channel") {
    const TableSpec spec{.source = 10,
                         .destination = 11,
                         .ranges = {{{0, 1, 3, 0, 127, 4}, {0, 1, 3, 0, 127, 4}, {0, 1, 3, 0, 127, 4}}}};
    const Bytes cntt = chunk("Cntt", Bytes{10, 0x81});
    const Style style = parseStyle(styleWith(casm({{"Main A", {ctab(spec), cntt}}})));
    CHECK(findSection(style, Section::Main, 0)->rules[10]->ranges[1].bassOn);
}

TEST_CASE("a table that plays on the Bass part has bass on") {
    const TableSpec spec{.source = 12,
                         .destination = 10,
                         .ranges = {{{0, 1, 3, 0, 127, 4}, {0, 1, 3, 0, 127, 4}, {0, 1, 3, 0, 127, 4}}}};
    const Style style = parseStyle(styleWith(casm({{"Main A", {ctb2(spec)}}})));
    CHECK(findSection(style, Section::Main, 0)->rules[12]->ranges[0].bassOn);
}

TEST_CASE("a style channel the CASM leaves out follows the default rule") {
    const Style style = parseStyle(styleWith({}));
    const auto& drums = findSection(style, Section::Main, 0)->rules[8];
    REQUIRE(drums);
    CHECK(drums->ranges[1].rule == NoteRule::RootFixed);
    CHECK(drums->ranges[1].table == NoteTable::Bypass);
    const auto& phrase = findSection(style, Section::Main, 0)->rules[15];
    CHECK(phrase->ranges[1].table == NoteTable::Chord);
    CHECK(phrase->ranges[1].highKey == 5);
    CHECK(findSection(style, Section::Main, 0)->rules[10]->ranges[1].bassOn);
    CHECK_FALSE(findSection(style, Section::Main, 0)->rules[3].has_value());
}

TEST_CASE("malformed styles throw a style error") {
    requireStyleError({});
    requireStyleError(Bytes{'M', 'T', 'r', 'k', 0, 0, 0, 0});
    requireStyleError(chunk("MThd", Bytes{0, 0, 0, 1}));
    requireStyleError(chunk("MThd", Bytes{0, 0, 0, 1, 0xE7, 0x28}));
    requireStyleError(chunk("MThd", Bytes{0, 0, 0, 1, 0, 96}));

    const Bytes header = chunk("MThd", Bytes{0, 0, 0, 1, 0, 96});
    requireStyleError(concat(header, chunk("MTrk", Bytes{0, 0x40, 0x7F})));
    requireStyleError(concat(header, chunk("MTrk", Bytes{0x81, 0x81, 0x81, 0x81, 0x01, 0x90, 0x40, 0x7F})));
    requireStyleError(concat(header, chunk("MTrk", Bytes{0, 0x90, 0x40})));
    requireStyleError(concat(header, chunk("MTrk", Bytes{0, 0x90, 0x40, 0x90})));
    requireStyleError(concat(header, chunk("MTrk", Bytes{0, 0xFF, 0x06, 0x10, 'M'})));
    requireStyleError(concat(header, chunk("MTrk", Bytes{0, 0xF8})));
    requireStyleError(concat(header, chunk("MTrk", Bytes{0, 0xFF, 0x2F, 0})));
    requireStyleError(concat(header, Bytes{'M', 'T', 'r', 'k', 0, 0, 1, 0, 0}));

    const TableSpec bad{
        .source = 8, .destination = 8, .ranges = {{{3, 0, 0, 0, 127, 0}, {3, 0, 0, 0, 127, 0}, {3, 0, 0, 0, 127, 0}}}};
    requireStyleError(styleWith(casm({{"Main A", {ctab(bad)}}})));
    requireStyleError(styleWith(casm({{"Main A", {ctab({.destination = 16})}}})));
    requireStyleError(styleWith(casm({{"Main A", {ctab({.sourceType = 34})}}})));
    requireStyleError(styleWith(casm({{"Main A", {chunk("Ctab", Bytes{8, 1, 2})}}})));
    requireStyleError(styleWith(casm({{"Main A", {chunk("Cntt", Bytes{8})}}})));
    const TableSpec badTable{.ranges = {{{0, 0x0B, 0, 0, 127, 0}, {0, 0x0B, 0, 0, 127, 0}, {0, 0x0B, 0, 0, 127, 0}}}};
    requireStyleError(styleWith(casm({{"Main A", {ctb2(badTable)}}})));
}

TEST_CASE("every truncation and corruption of a style parses or throws a style error") {
    const Bytes style = demoStyle();
    for (std::size_t size = 0; size < style.size(); ++size) {
        try {
            parseStyle(std::span(style).first(size));
        } catch (const StyleError&) {
            SUCCEED();
        }
    }
    std::uint32_t seed = 12345;
    for (int round = 0; round < 2000; ++round) {
        Bytes corrupt = style;
        for (int flip = 0; flip < 4; ++flip) {
            seed = seed * 1664525U + 1013904223U;
            corrupt[seed % corrupt.size()] = static_cast<std::uint8_t>(seed >> 24);
        }
        try {
            parseStyle(corrupt);
        } catch (const StyleError&) {
            SUCCEED();
        }
    }
}
