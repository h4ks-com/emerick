#include "emerick/yamaha/render.hpp"

#include <catch2/catch_test_macros.hpp>

#include "emerick/yamaha/sysex.hpp"

using namespace emerick::yamaha;
using Bytes = std::vector<std::uint8_t>;

namespace {

constexpr emerick::core::Ticks kBeat = 960;
constexpr emerick::core::Ticks kBar = 4 * kBeat;

ChordEvent chordAt(emerick::core::Ticks at, const char* name) { return {at, *Chord::parse(name)}; }

std::vector<int> pressed(const std::vector<SectionEvent>& sections) {
    std::vector<int> buttons;
    for (const RawEvent& event : sectionPresses(sections, 16, kBeat)) {
        CHECK(event.bytes.at(0) == 0xBF);
        if (event.bytes.at(2) == 127) {
            buttons.push_back(event.bytes.at(1));
        }
    }
    return buttons;
}

}  // namespace

TEST_CASE("chords play as Chord Control sysex and never as notes") {
    const auto events = chordControls({chordAt(0, "C"), chordAt(3840, "Dm7"), chordAt(7680, "C/E")});
    CHECK(events == std::vector<RawEvent>{{0, Bytes{0xF0, 0x43, 0x7E, 0x02, 0x31, 0x00, 0x7F, 0x7F, 0xF7}},
                                          {3840, Bytes{0xF0, 0x43, 0x7E, 0x02, 0x32, 0x0A, 0x7F, 0x7F, 0xF7}},
                                          {7680, Bytes{0xF0, 0x43, 0x7E, 0x02, 0x31, 0x00, 0x33, 0x7F, 0xF7}}});
    for (const RawEvent& event : chordControls({chordAt(0, "Bbmaj7/D"), chordAt(960, "N.C.")})) {
        CHECK(decodeChordControl(event.bytes));
    }
}

TEST_CASE("a Main presses its button a beat before its bar and a break presses at once, down then up") {
    const auto events = sectionPresses(
        {{0, Section::Main, 0}, {2 * kBar, Section::Main, 1}, {3 * kBar + kBeat, Section::Break, 0}}, 16, kBeat);
    CHECK(events == std::vector<RawEvent>{{0, Bytes{0xBF, 11, 127}},
                                          {10, Bytes{0xBF, 11, 0}},
                                          {2 * kBar - kBeat, Bytes{0xBF, 12, 127}},
                                          {2 * kBar - kBeat + 10, Bytes{0xBF, 12, 0}},
                                          {3 * kBar + kBeat, Bytes{0xBF, 8, 127}},
                                          {3 * kBar + kBeat + 10, Bytes{0xBF, 8, 0}}});
    CHECK(sectionPresses({{0, Section::Main, 3}}, 1, kBeat).front().bytes == Bytes{0xB0, 14, 127});
}

TEST_CASE("every section kind presses the External Controller button the keyboard assigns to it") {
    CHECK(pressed({{0, Section::Intro, 2}, {kBar, Section::Main, 3}, {2 * kBar, Section::Ending, 1}}) ==
          std::vector<int>{23, 14, 32});
    CHECK(pressed({{0, Section::Intro, 0}, {kBar, Section::Ending, 0}, {2 * kBar, Section::Main, 2}}) ==
          std::vector<int>{21, 31, 13});
}

TEST_CASE("a fill presses Fill Self, Fill Up, Fill Down or the Main it leads to, and its Main needs no press") {
    CHECK(pressed({{0, Section::Main, 0},
                   {kBar, Section::Fill, 0},
                   {2 * kBar, Section::Main, 0},
                   {3 * kBar, Section::FillTo, 1},
                   {4 * kBar, Section::Main, 1},
                   {5 * kBar, Section::FillTo, 0},
                   {6 * kBar, Section::Main, 0},
                   {7 * kBar, Section::FillTo, 3},
                   {8 * kBar, Section::Main, 3}}) == std::vector<int>{11, 6, 9, 5, 14});
    CHECK(pressed({{0, Section::FillTo, 2}}) == std::vector<int>{13});
}

TEST_CASE("chase selects the section a position plays, and a fill selects the Main it leads to") {
    const std::vector<SectionEvent> sections{
        {kBar, Section::Intro, 0},    {2 * kBar, Section::Main, 0},   {4 * kBar + kBeat, Section::Fill, 0},
        {5 * kBar, Section::Main, 0}, {6 * kBar, Section::FillTo, 1}, {7 * kBar, Section::Break, 0}};
    const auto chased = [&](emerick::core::Ticks at) { return chaseSection(sections, at); };
    CHECK_FALSE(chased(0));
    CHECK(chased(kBar)->kind == Section::Intro);
    CHECK(chased(3 * kBar)->kind == Section::Main);
    CHECK(chased(4 * kBar + 2 * kBeat) == SectionEvent{4 * kBar + kBeat, Section::Main, 0});
    CHECK(chased(6 * kBar + kBeat)->kind == Section::Main);
    CHECK(chased(6 * kBar + kBeat)->variation == 1);
    CHECK(chased(7 * kBar)->variation == 1);
    CHECK(sectionPress(*chased(6 * kBar), 16) == std::vector<Bytes>{{0xBF, 12, 127}, {0xBF, 12, 0}});
    CHECK(buttonPress(kStartStopCc, 1) == std::vector<Bytes>{{0xB0, 0, 127}, {0xB0, 0, 0}});
}

TEST_CASE("a Main's pattern repeats every pass from its first downbeat, and an Intro or Ending plays once") {
    const std::vector<SectionEvent> sections{{0, Section::Intro, 0},
                                             {2 * kBar, Section::Main, 1, 4},
                                             {12 * kBar, Section::Main, 2},
                                             {14 * kBar, Section::Ending, 0}};
    const auto at = [&](emerick::core::Ticks position) { return *patternAt(sections, position, kBar); };
    CHECK(at(kBar + kBeat).passStart == 0);
    CHECK(at(kBar + kBeat).bar == 2);
    CHECK(at(kBar + kBeat).bars == 0);
    CHECK(at(8 * kBar + kBeat).passStart == 6 * kBar);
    CHECK(at(8 * kBar + kBeat).bar == 3);
    CHECK(at(8 * kBar + kBeat).bars == 4);
    CHECK(at(13 * kBar + 3 * kBeat).passStart == 13 * kBar);
    CHECK(at(13 * kBar + 3 * kBeat).bars == 1);
    CHECK(at(15 * kBar).passStart == 14 * kBar);
    CHECK_FALSE(patternAt({{kBar, Section::Main, 0}}, 0, kBar));
}
