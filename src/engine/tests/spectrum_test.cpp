#include "emerick/engine/spectrum.hpp"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

using Catch::Approx;

TEST_CASE("the spectrum of a sine peaks at its bin at its level") {
    constexpr std::size_t kSize = 1024;
    constexpr std::size_t kBin = 64;
    std::vector<float> frame(kSize);
    for (std::size_t i = 0; i < kSize; ++i) {
        frame[i] = static_cast<float>(
            0.5 * std::sin(2 * std::numbers::pi * kBin * static_cast<double>(i) / static_cast<double>(kSize)));
    }
    const std::vector<double> db = emerick::engine::spectrumDb(frame);
    REQUIRE(db.size() == (kSize / 2) + 1);
    CHECK(static_cast<std::size_t>(std::ranges::max_element(db) - db.begin()) == kBin);
    CHECK(db[kBin] == Approx(20 * std::log10(0.5)).margin(0.01));
    CHECK(db[kBin + 10] < -100);
}
