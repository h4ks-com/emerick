#include "emerick/core/join.hpp"

#include <catch2/catch_test_macros.hpp>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

using emerick::core::join;

TEST_CASE("join puts the separator between parts and keeps empty parts") {
    CHECK(join(std::vector<std::string>{}, ", ").empty());
    CHECK(join(std::vector<std::string_view>{"one"}, ", ") == "one");
    CHECK(join(std::vector<std::string>{"one", "", "three"}, ", ") == "one, , three");
    CHECK(join(std::vector{1, 2} | std::views::transform([](int n) { return std::to_string(n); }), "-") == "1-2");
}
