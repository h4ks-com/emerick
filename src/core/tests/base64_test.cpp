#include "emerick/core/base64.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace emerick::core;

namespace {

std::vector<std::uint8_t> bytesOf(std::string_view text) { return {text.begin(), text.end()}; }

}  // namespace

TEST_CASE("base64 writes the RFC 4648 test vectors and reads them back") {
    for (const auto& [plain, encoded] :
         {std::pair{"", ""}, std::pair{"f", "Zg=="}, std::pair{"fo", "Zm8="}, std::pair{"foo", "Zm9v"},
          std::pair{"foob", "Zm9vYg=="}, std::pair{"fooba", "Zm9vYmE="}, std::pair{"foobar", "Zm9vYmFy"}}) {
        CHECK(toBase64(bytesOf(plain)) == encoded);
        CHECK(fromBase64(encoded) == bytesOf(plain));
    }
}

TEST_CASE("base64 refuses text outside its alphabet or unpadded") {
    CHECK_FALSE(fromBase64("Zm9").has_value());
    CHECK_FALSE(fromBase64("Zm9*").has_value());
    CHECK_FALSE(fromBase64("Zg==Zm9v").has_value());
}
