#include "emerick/yamaha/profile.hpp"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

using namespace emerick::yamaha;
using Bytes = std::vector<std::uint8_t>;

TEST_CASE("the Genos2 profile names every channel as the keyboard transmits it") {
    const auto& parts = genos2Parts();
    CHECK(parts[0].name == "Right 1");
    CHECK(parts[1].name == "Right 2");
    CHECK(parts[2].name == "Right 3");
    CHECK(parts[3].name == "Left");
    CHECK(parts[4].name == "Multi Pad 1");
    CHECK(parts.front().icon == "piano-keys");
    CHECK(parts[10].name == "Bass");
    CHECK(parts[10].icon == "guitar");
    CHECK(parts.back().name == "Phrase 2");
    for (std::size_t i = 0; i < parts.size(); ++i) {
        CHECK(parts[i].channel == static_cast<int>(i) + 1);
    }
}

TEST_CASE("a voice file loads") {
    const auto voices = loadVoices(std::string(TEST_DATA_DIR) + "/voices.txt");
    REQUIRE(voices.size() == 3);
    CHECK(voices[0] == Voice{9, 127, 0, 1, 100, 64, "Standard Kit"});
    CHECK(voices[2] == Voice{16, 0, 0, 61, 80, 58, "French Horn"});
}

TEST_CASE("bad voice lines are rejected") {
    CHECK_THROWS_AS(parseVoices("9 127 0 59 49"), std::invalid_argument);
    CHECK_THROWS_AS(parseVoices("17 127 0 59 49 64 X"), std::invalid_argument);
    CHECK_THROWS_AS(parseVoices("9 127 0 129 49 64 X"), std::invalid_argument);
    CHECK_THROWS_AS(loadVoices("/nonexistent/voices.txt"), std::runtime_error);
}

TEST_CASE("a voice round trips through json and json out of range is rejected") {
    const Voice voice{9, 127, 0, 1, 100, 64, "Standard Kit"};
    const nlohmann::json json = voice;
    CHECK(json.at("program") == 1);
    CHECK(json.get<Voice>() == voice);
    nlohmann::json bad = json;
    bad["channel"] = 0;
    CHECK_THROWS_AS(bad.get<Voice>(), std::invalid_argument);
}

TEST_CASE("a voice renders bank, program, volume and pan spaced by the gap") {
    const auto events = voiceMessages(parseVoices("# header\n\n12 11 1 2 78 64 AmericanBallad (FM)\n"), 100, 5);
    CHECK(events == std::vector<RawEvent>{{100, Bytes{0xBB, 0, 11}},
                                          {105, Bytes{0xBB, 32, 1}},
                                          {110, Bytes{0xCB, 1}},
                                          {115, Bytes{0xBB, 7, 78}},
                                          {120, Bytes{0xBB, 10, 64}}});
}
