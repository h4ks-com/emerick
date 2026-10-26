#include <catch2/catch_test_macros.hpp>

#include "emerick/engine/clap_scan.hpp"

using namespace emerick::engine;

TEST_CASE("a CLAP bundle lists its plugins with their features") {
    const auto described = describeClapBundle(EMERICK_TEST_SINE_CLAP);
    REQUIRE(described.has_value());
    REQUIRE(described->size() == 1);
    CHECK(described->front().id == "org.emerick.test.sine");
    CHECK(described->front().name == "Test Sine");
    CHECK(described->front().features == std::vector<std::string>{"instrument", "synthesizer"});
}

TEST_CASE("a file that is no CLAP bundle reports why") {
    const auto described = describeClapBundle("/no/such/plugin.clap");
    REQUIRE_FALSE(described.has_value());
    CHECK(described.error().find("/no/such/plugin.clap") != std::string::npos);
}
