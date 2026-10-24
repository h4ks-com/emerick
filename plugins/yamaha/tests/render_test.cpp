#include "emerick/yamaha/render.hpp"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "emerick/yamaha/sysex.hpp"

using namespace emerick::yamaha;
using Bytes = std::vector<std::uint8_t>;

namespace {

ChordEvent chordAt(emerick::core::Ticks at, const char* name) { return {at, *Chord::parse(name)}; }

Bytes section(std::uint8_t code, std::uint8_t on) { return {0xF0, 0x43, 0x7E, 0x00, code, on, 0xF7}; }

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

TEST_CASE("each section presses its Section Control switch, On then Off ten ticks later") {
    const auto events = sectionControls({{0, Section::Main, 1}, {3840, Section::Break, 0}});
    CHECK(events == std::vector<RawEvent>{{0, section(0x09, 0x7F)},
                                          {10, section(0x09, 0x00)},
                                          {3840, section(0x18, 0x7F)},
                                          {3850, section(0x18, 0x00)}});
}

TEST_CASE("every section kind sends the switch the keyboard records for it") {
    const auto on = [](const std::vector<SectionEvent>& sections) {
        std::vector<Bytes> pressed;
        for (const RawEvent& event : sectionControls(sections)) {
            if (event.bytes[5] == 0x7F) {
                pressed.push_back(event.bytes);
            }
        }
        return pressed;
    };
    CHECK(on({{0, Section::Intro, 2}, {1, Section::Main, 3}, {2, Section::FillTo, 2}, {3, Section::Ending, 1}}) ==
          std::vector<Bytes>{section(0x02, 0x7F), section(0x0B, 0x7F), section(0x12, 0x7F), section(0x21, 0x7F)});
    CHECK(on({{0, Section::Fill, 0}, {100, Section::Main, 2}, {200, Section::Fill, 0}}) ==
          std::vector<Bytes>{section(0x10, 0x7F), section(0x0A, 0x7F), section(0x12, 0x7F)});
    CHECK(on({{0, Section::FillTo, 3}, {100, Section::Fill, 0}}).back() == section(0x13, 0x7F));
}

TEST_CASE("a Fill has no Section Control switch of its own") {
    CHECK_THROWS_AS(encodeSectionControl({Section::Fill, 0, true}), std::invalid_argument);
}
