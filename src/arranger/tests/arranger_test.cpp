#include "emerick/arranger/arranger.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <iterator>
#include <map>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "emerick/arranger/style.hpp"
#include "emerick/core/time.hpp"
#include "emerick/yamaha/chord.hpp"
#include "emerick/yamaha/section.hpp"
#include "style_builder.hpp"

using namespace emerick::arranger;
using namespace emerick::arranger::test;
using emerick::core::Ticks;
using emerick::yamaha::Chord;
using emerick::yamaha::Section;

namespace {

constexpr Ticks kBeat = 960;
constexpr Ticks kBar = 4 * kBeat;

const Style& demo() {
    static const Style style = parseStyle(demoStyle());
    return style;
}

std::vector<MidiEvent> renderAll(Arranger& arranger, Ticks from, Ticks to, Ticks block = 240) {
    std::vector<MidiEvent> result;
    for (Ticks at = from; at < to; at += block) {
        const auto events = arranger.render(at, std::min(at + block, to));
        result.insert(result.end(), events.begin(), events.end());
    }
    return result;
}

std::vector<std::pair<Ticks, int>> notesOn(const std::vector<MidiEvent>& events, int channel) {
    std::vector<std::pair<Ticks, int>> result;
    for (const auto& event : events) {
        if (event.bytes[0] == (0x90 | channel) && event.bytes[2] > 0) {
            result.emplace_back(event.at, event.bytes[1]);
        }
    }
    return result;
}

std::vector<std::pair<Ticks, int>> beats(std::initializer_list<std::pair<int, int>> beatNotes) {
    std::vector<std::pair<Ticks, int>> result;
    for (const auto& [beat, note] : beatNotes) {
        result.emplace_back(beat * kBeat, note);
    }
    return result;
}

Chord chord(std::string_view name) { return *Chord::parse(name); }

}  // namespace

TEST_CASE("starting sends each part's setup controllers and program in the style's order") {
    Arranger arranger(demo());
    arranger.start(0);
    const auto events = renderAll(arranger, 0, 1);
    const auto has = [&](MidiEvent event) { return std::ranges::find(events, event) != events.end(); };
    CHECK(has({0, {0xB8, 0, 127}}));
    CHECK(has({0, {0xC8, 0, 0}}));
    CHECK(has({0, {0xB8, 7, 110}}));
    CHECK(has({0, {0xBB, 32, 2}}));
    std::vector<MidiEvent> bass;
    std::ranges::copy_if(events, std::back_inserter(bass), [](const MidiEvent& event) {
        return (event.bytes[0] == 0xBA || event.bytes[0] == 0xCA) && event.at == 0;
    });
    std::vector<MidiEvent> expected{
        {0, {0xBA, 0, 0}}, {0, {0xBA, 32, 0}}, {0, {0xCA, 33, 0}}, {0, {0xBA, 7, 100}}, {0, {0xBA, 10, 60}}};
    for (const auto& [cc, value] : kBassMix) {
        expected.push_back({0, {0xBA, cc, value}});
    }
    CHECK(bass == expected);
}

TEST_CASE("a Main loops until something else is requested") {
    Arranger arranger(demo());
    arranger.start(0);
    const auto hits = notesOn(renderAll(arranger, 0, 2 * 2 * kBar), kDrums);
    REQUIRE(hits.size() == 16);
    for (std::size_t index = 0; index < hits.size(); ++index) {
        CHECK(hits[index] == std::pair{static_cast<Ticks>(index) * kBeat, int{kMainADrum}});
    }
}

TEST_CASE("a fill starts at the next beat, plays the rest of its bar and lands on the Main at the bar line") {
    Arranger arranger(demo());
    arranger.start(0);
    auto events = renderAll(arranger, 0, 1000);
    arranger.request(Section::Main, 1, 1000);
    const auto rest = renderAll(arranger, 1000, 2 * kBar);
    events.insert(events.end(), rest.begin(), rest.end());
    CHECK(notesOn(events, kDrums) == beats({{0, kMainADrum},
                                            {1, kMainADrum},
                                            {2, kFillBDrum},
                                            {3, kFillBDrum},
                                            {4, kMainBDrum},
                                            {5, kMainBDrum},
                                            {6, kMainBDrum},
                                            {7, kMainBDrum}}));
    CHECK(arranger.currentSection() == findSection(demo(), Section::Main, 1));
}

TEST_CASE("requesting the Main that plays plays its own fill") {
    Arranger arranger(demo());
    arranger.start(0);
    auto events = renderAll(arranger, 0, 100);
    arranger.request(Section::Main, 0, 100);
    const auto rest = renderAll(arranger, 100, kBar + kBeat);
    events.insert(events.end(), rest.begin(), rest.end());
    CHECK(notesOn(events, kDrums) ==
          beats({{0, kMainADrum}, {1, kFillADrum}, {2, kFillADrum}, {3, kFillADrum}, {4, kMainADrum}}));
}

TEST_CASE("without auto fill a Main switches at the next bar line") {
    Arranger arranger(demo());
    arranger.setAutoFill(false);
    arranger.start(0);
    auto events = renderAll(arranger, 0, 1000);
    arranger.request(Section::Main, 1, 1000);
    const auto rest = renderAll(arranger, 1000, kBar + kBeat);
    events.insert(events.end(), rest.begin(), rest.end());
    CHECK(notesOn(events, kDrums) ==
          beats({{0, kMainADrum}, {1, kMainADrum}, {2, kMainADrum}, {3, kMainADrum}, {4, kMainBDrum}}));
}

TEST_CASE("a break plays from the next beat and returns to the Main it interrupted") {
    Arranger arranger(demo());
    arranger.request(Section::Main, 1, 0);
    arranger.start(0);
    auto events = renderAll(arranger, 0, 2000);
    arranger.request(Section::Break, 0, 2000);
    const auto rest = renderAll(arranger, 2000, kBar + kBeat);
    events.insert(events.end(), rest.begin(), rest.end());
    CHECK(notesOn(events, kDrums) ==
          beats({{0, kMainBDrum}, {1, kMainBDrum}, {2, kMainBDrum}, {3, kBreakDrum}, {4, kMainBDrum}}));
}

TEST_CASE("an intro plays once and goes to the requested Main") {
    Arranger arranger(demo());
    arranger.request(Section::Intro, 0, 0);
    arranger.request(Section::Main, 1, 0);
    arranger.start(0);
    CHECK(notesOn(renderAll(arranger, 0, kBar + kBeat), kDrums) ==
          beats({{0, kIntroDrum}, {1, kIntroDrum}, {2, kIntroDrum}, {3, kIntroDrum}, {4, kMainBDrum}}));
}

TEST_CASE("an intro with no Main requested goes to Main A") {
    Arranger arranger(demo());
    arranger.request(Section::Intro, 0, 0);
    arranger.start(0);
    const auto hits = notesOn(renderAll(arranger, 0, kBar + kBeat), kDrums);
    CHECK(hits.back() == std::pair{kBar, int{kMainADrum}});
}

TEST_CASE("an ending starts at the next bar line, plays once and stops") {
    Arranger arranger(demo());
    arranger.start(0);
    auto events = renderAll(arranger, 0, 1000);
    arranger.request(Section::Ending, 0, 1000);
    const auto rest = renderAll(arranger, 1000, 4 * kBar);
    events.insert(events.end(), rest.begin(), rest.end());
    const auto hits = notesOn(events, kDrums);
    REQUIRE(hits.size() == 8);
    CHECK(hits[4] == std::pair{kBar, int{kEndingDrum}});
    CHECK(hits[7] == std::pair{kBar + 3 * kBeat, int{kEndingDrum}});
    CHECK_FALSE(arranger.playing());
    CHECK(std::ranges::all_of(events, [](const MidiEvent& event) { return event.at <= 2 * kBar; }));
}

TEST_CASE("a chord change transposes the parts and moves held bass notes to the new root") {
    Arranger arranger(demo());
    arranger.setChord(chord("C"), 0);
    arranger.start(0);
    CHECK(notesOn(renderAll(arranger, 0, 480), kBass) == std::vector<std::pair<Ticks, int>>{{0, 36}});
    arranger.setChord(chord("F"), 480);
    const auto events = renderAll(arranger, 480, kBeat + 1);
    REQUIRE(events.size() >= 3);
    CHECK(events[0] == MidiEvent{480, {0x8A, 36, 0}});
    CHECK(events[1] == MidiEvent{480, {0x9A, 41, 100}});
    CHECK(std::ranges::find(events, MidiEvent{kBeat, {0x8A, 41, 0}}) != events.end());
}

TEST_CASE("the bass plays the bass note of a slash chord and chord parts follow the chord type") {
    Arranger arranger(demo());
    arranger.setChord(chord("Dm7/C"), 0);
    arranger.start(0);
    const auto events = renderAll(arranger, 0, kBeat);
    CHECK(notesOn(events, kBass) == std::vector<std::pair<Ticks, int>>{{0, 36}});
    CHECK(notesOn(events, kChords) == std::vector<std::pair<Ticks, int>>{{0, 65}, {0, 69}, {0, 72}});
    Arranger slash(demo());
    slash.setChord(chord("C/E"), 0);
    slash.start(0);
    CHECK(notesOn(renderAll(slash, 0, kBeat), kBass) == std::vector<std::pair<Ticks, int>>{{0, 40}});
}

TEST_CASE("with no chord only the rhythm parts play") {
    Arranger arranger(demo());
    arranger.start(0);
    const auto events = renderAll(arranger, 0, kBar);
    CHECK(notesOn(events, kDrums).size() == 4);
    CHECK(notesOn(events, kChords).empty());
    CHECK(notesOn(events, kBass).empty());
}

TEST_CASE("part controllers pass through on the part's channel") {
    Arranger arranger(demo());
    arranger.start(0);
    const auto events = renderAll(arranger, 0, kBeat);
    CHECK(std::ranges::find(events, MidiEvent{0, {0xBD, 11, 90}}) != events.end());
}

TEST_CASE("every note the arranger starts it also ends") {
    Arranger arranger(demo());
    arranger.setChord(chord("C"), 0);
    arranger.start(0);
    auto events = renderAll(arranger, 0, 3000);
    arranger.setChord(chord("Bbm7"), 3000);
    arranger.request(Section::Main, 1, 3000);
    const auto more = renderAll(arranger, 3000, 7000);
    events.insert(events.end(), more.begin(), more.end());
    arranger.stop(7000);
    const auto last = renderAll(arranger, 7000, 7001);
    events.insert(events.end(), last.begin(), last.end());
    std::map<std::pair<int, int>, int> sounding;
    for (const auto& event : events) {
        const int type = event.bytes[0] & 0xF0;
        const std::pair key{event.bytes[0] & 0x0F, int{event.bytes[1]}};
        if (type == 0x90 && event.bytes[2] > 0) {
            ++sounding[key];
        } else if (type == 0x80) {
            --sounding[key];
        }
    }
    CHECK(std::ranges::all_of(sounding, [](const auto& entry) { return entry.second == 0; }));
}

TEST_CASE("rendering is the same in any block size") {
    const auto play = [](Ticks block) {
        Arranger arranger(demo());
        arranger.setChord(chord("G7"), 0);
        arranger.start(0);
        auto events = renderAll(arranger, 0, 1200, block);
        arranger.request(Section::Main, 1, 1200);
        const auto rest = renderAll(arranger, 1200, 3 * kBar, block);
        events.insert(events.end(), rest.begin(), rest.end());
        return events;
    };
    CHECK(play(1200) == play(7));
    CHECK(play(1200) == play(100));
}
