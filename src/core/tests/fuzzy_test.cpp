#include "emerick/core/fuzzy.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace emerick::core;

namespace {

struct Entry {
    std::string title;
    std::string alias;
    std::string id;
    std::string keyword;
};

std::vector<std::string> rank(std::string_view query, const std::vector<Entry>& entries) {
    std::vector<std::pair<double, std::string>> hits;
    for (const Entry& entry : entries) {
        const std::array fields{FuzzyField{entry.title, 1.0}, FuzzyField{entry.alias, 0.9}, FuzzyField{entry.id, 0.7},
                                FuzzyField{entry.keyword, 0.6}};
        if (auto match = fuzzyMatchFields(query, fields)) {
            hits.emplace_back(match->score, entry.title);
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> titles;
    for (auto& hit : hits) {
        titles.push_back(std::move(hit.second));
    }
    return titles;
}

const std::vector<Entry> kEntries{
    {"Pre-roll", "count in", "transport.preroll", "recording"},
    {"Record", "", "transport.record", ""},
    {"Audio buffer size", "latency", "settings.audio.bufferSize", "block"},
    {"Audio input", "", "settings.audio.input", ""},
    {"Play", "start", "transport.play", ""},
    {"Open settings", "preferences", "settings.open", "options"},
};

}  // namespace

TEST_CASE("rec ranks Record above Pre-roll") {
    const auto titles = rank("rec", kEntries);
    REQUIRE(titles.size() >= 2);
    CHECK(titles[0] == "Record");
    CHECK(titles[1] == "Pre-roll");
}

TEST_CASE("aliases and keywords match") {
    CHECK(rank("latency", kEntries) == std::vector<std::string>{"Audio buffer size"});
    CHECK(rank("prefs", kEntries).front() == "Open settings");
    CHECK(rank("count", kEntries).front() == "Pre-roll");
}

TEST_CASE("terms are ANDed in any order") {
    CHECK(rank("audio buf", kEntries).front() == "Audio buffer size");
    CHECK(rank("buf audio", kEntries).front() == "Audio buffer size");
    CHECK(rank("audio zzz", kEntries).empty());
}

TEST_CASE("word starts beat letters inside words") {
    CHECK(rank("abs", kEntries).front() == "Audio buffer size");
    CHECK(rank("pl", kEntries).front() == "Play");
}

TEST_CASE("camelCase humps score as boundaries") {
    const auto hump = fuzzyMatch("bs", "bufferSize");
    const auto inside = fuzzyMatch("bs", "absorbs");
    REQUIRE(hump);
    REQUIRE(inside);
    CHECK(hump->score > inside->score);
}

TEST_CASE("smart case") {
    CHECK(fuzzyMatch("record", "Record"));
    CHECK(fuzzyMatch("Rec", "Record"));
    CHECK_FALSE(fuzzyMatch("REC", "Record"));
}

TEST_CASE("ranges cover the matched characters of the first field") {
    const auto match = fuzzyMatch("abs", "Audio buffer size");
    REQUIRE(match);
    CHECK(match->ranges == std::vector<MatchRange>{{0, 1}, {6, 7}, {13, 14}});
    CHECK(fuzzyMatch("rec", "Record")->ranges == std::vector<MatchRange>{{0, 3}});
}

TEST_CASE("an empty query matches everything with no ranges") {
    const auto match = fuzzyMatch("", "Record");
    REQUIRE(match);
    CHECK(match->ranges.empty());
}
