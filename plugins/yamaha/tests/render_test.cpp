#include "emerick/yamaha/render.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace emerick::yamaha;
using Bytes = std::vector<std::uint8_t>;

namespace {

ChordEvent chordAt(emerick::core::Ticks at, const char* name) { return {at, *Chord::parse(name)}; }

}  // namespace

TEST_CASE("chords sound as block notes until the next chord") {
    const auto events = chordNotes({chordAt(0, "C"), chordAt(3840, "Dm")}, kChordDetectChannel, 7680);
    REQUIRE(events.size() == 12);
    CHECK(events.front() == RawEvent{0, Bytes{0x93, 48, 90}});
    CHECK(events[2] == RawEvent{0, Bytes{0x93, 55, 90}});
    CHECK(events[3] == RawEvent{3840, Bytes{0x83, 48, 0}});
    CHECK(events[6] == RawEvent{3840, Bytes{0x93, 50, 90}});
    CHECK(events.back() == RawEvent{7680, Bytes{0x83, 57, 0}});
}

TEST_CASE("a slash chord adds its bass below and N.C. is silent") {
    const auto events = chordNotes({chordAt(0, "C/E"), chordAt(960, "N.C.")}, 4, 1920);
    REQUIRE(events.size() == 8);
    CHECK(events.front().bytes == Bytes{0x93, 40, 90});
    CHECK(chordNotes({chordAt(0, "N.C.")}, 4, 960).empty());
}

TEST_CASE("sections press their External Controller buttons") {
    CHECK(sectionCc({0, Section::Main, 0}) == 11);
    CHECK(sectionCc({0, Section::Main, 3}) == 14);
    CHECK(sectionCc({0, Section::Fill, 0}) == 6);
    CHECK(sectionCc({0, Section::FillTo, 2}) == 13);
    CHECK(sectionCc({0, Section::Break, 0}) == 8);
    CHECK(sectionCc({0, Section::Intro, 0}) == 21);
    CHECK(sectionCc({0, Section::Intro, 2}) == 23);
    CHECK(sectionCc({0, Section::Ending, 0}) == 31);
    CHECK(sectionCc({0, Section::Ending, 2}) == 33);
}

TEST_CASE("each button press is value 127 then 0 ten ticks later") {
    const auto events = sectionControls({{0, Section::Main, 1}, {3840, Section::Break, 0}}, 1);
    CHECK(events == std::vector<RawEvent>{{0, Bytes{0xB0, 12, 127}},
                                          {10, Bytes{0xB0, 12, 0}},
                                          {3840, Bytes{0xB0, 8, 127}},
                                          {3850, Bytes{0xB0, 8, 0}}});
}
