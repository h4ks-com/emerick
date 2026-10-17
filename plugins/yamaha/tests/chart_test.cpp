#include "emerick/yamaha/chart.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iterator>
#include <libremidi/reader.hpp>
#include <sstream>

using namespace emerick::yamaha;
using emerick::core::kTicksPerQuarter;
using emerick::core::TempoMap;
using Bytes = std::vector<std::uint8_t>;

namespace {

const TempoMap kTempo{96.0, {4, 4}};
const emerick::core::Ticks kBeat = kTicksPerQuarter;
const emerick::core::Ticks kBar = 4 * kBeat;

std::string readText(const std::string& name) {
    std::ifstream file(std::string(TEST_DATA_DIR) + "/" + name);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

Bytes readBytes(const std::string& name) {
    std::ifstream file(std::string(TEST_DATA_DIR) + "/" + name, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<RawEvent> sysexOfTake(const std::string& name) {
    const Bytes bytes = readBytes(name);
    libremidi::reader reader(true);
    REQUIRE(reader.parse(bytes.data(), bytes.size()) >= libremidi::reader::complete);
    std::vector<RawEvent> events;
    for (const auto& track : reader.tracks) {
        for (const auto& event : track) {
            if (event.m.bytes.front() == 0xF0) {
                const auto at = std::llround(static_cast<double>(event.tick) * kTicksPerQuarter / reader.ticksPerBeat);
                events.push_back({at, Bytes(event.m.bytes.begin(), event.m.bytes.end())});
            }
        }
        if (!events.empty()) {
            break;
        }
    }
    return events;
}

RawEvent chord(emerick::core::Ticks at, std::uint8_t root, std::uint8_t type) {
    return {at, {0xF0, 0x43, 0x7E, 0x02, root, type, 0x7F, 0x7F, 0xF7}};
}

RawEvent section(emerick::core::Ticks at, std::uint8_t code, std::uint8_t on = 0x7F) {
    return {at, {0xF0, 0x43, 0x7E, 0x00, code, on, 0xF7}};
}

}  // namespace

TEST_CASE("a take with a one bar shift matches the reference chart") {
    const auto chart = chartFromTake(sysexOfTake("take.mid"), kTempo, {.shiftBars = 1});
    CHECK(chartText(chart, kTempo) == readText("take-shift1.txt"));
}

TEST_CASE("chord changes snap to the beat and beat 4 moves to the next downbeat") {
    const auto chart =
        chartFromTake({chord(kBeat + 100, 0x31, 0x00), chord(kBar + 3 * kBeat - 90, 0x32, 0x08)}, kTempo, {});
    REQUIRE(chart.chords.size() == 2);
    CHECK(chart.chords[0].at == kBeat);
    CHECK(chart.chords[1].at == 2 * kBar);
}

TEST_CASE("the options turn snapping and the anticipation rule off") {
    const std::vector<RawEvent> events{chord(kBeat + 100, 0x31, 0x00), chord(kBar + 3 * kBeat, 0x32, 0x08)};
    const auto noAnticipation = chartFromTake(events, kTempo, {.anticipation = false});
    CHECK(noAnticipation.chords[1].at == kBar + 3 * kBeat);
    const auto unsnapped = chartFromTake(events, kTempo, {.snapToBeat = false, .anticipation = false});
    CHECK(unsnapped.chords[0].at == kBeat + 100);
}

TEST_CASE("repeated chords are dropped and the last chord at a position wins") {
    const auto chart = chartFromTake(
        {chord(0, 0x31, 0x00), chord(kBeat, 0x31, 0x00), chord(kBar, 0x32, 0x08), chord(kBar + 10, 0x33, 0x08)}, kTempo,
        {});
    REQUIRE(chart.chords.size() == 2);
    CHECK(chart.chords[0].chord.name() == "C");
    CHECK(chart.chords[1].chord.name() == "Em");
}

TEST_CASE("the first main starts the chart and a fill to the same main is a self fill") {
    const auto chart = chartFromTake({section(0, 0x09), section(kBar, 0x11), section(2 * kBar, 0x12),
                                      section(3 * kBar, 0x0A), section(4 * kBar, 0x0B), section(5 * kBar, 0x18)},
                                     kTempo, {});
    CHECK(chart.sections == std::vector<SectionEvent>{{0, Section::Main, 1},
                                                      {kBar, Section::Fill, 1},
                                                      {2 * kBar, Section::FillTo, 2},
                                                      {4 * kBar, Section::Main, 3},
                                                      {5 * kBar, Section::Break, 0}});
}

TEST_CASE("button releases do not make sections") {
    const auto chart = chartFromTake({section(0, 0x08), section(10, 0x08, 0x00), section(kBar, 0x00)}, kTempo, {});
    CHECK(chart.sections == std::vector<SectionEvent>{{0, Section::Main, 0}, {kBar, Section::Intro, 0}});
}

TEST_CASE("charts round trip through json") {
    const std::vector<ChordEvent> chords{{0, *Chord::parse("Dm7")}, {kBar, *Chord::parse("C/E")}};
    const std::vector<SectionEvent> sections{
        {0, Section::Main, 2}, {kBar, Section::FillTo, 1}, {2 * kBar, Section::Break, 0}};
    CHECK(nlohmann::json(chords).get<std::vector<ChordEvent>>() == chords);
    CHECK(nlohmann::json(sections).get<std::vector<SectionEvent>>() == sections);
    CHECK(nlohmann::json(chords)[0]["chord"] == "Dm7");
    CHECK_THROWS_AS(nlohmann::json::parse(R"({"at":0,"chord":"Zz"})").get<ChordEvent>(), std::invalid_argument);
    CHECK_THROWS_AS(nlohmann::json::parse(R"({"at":0,"kind":"x","variation":0})").get<SectionEvent>(),
                    std::invalid_argument);
}
