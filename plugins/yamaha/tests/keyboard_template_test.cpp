#include "emerick/yamaha/keyboard_template.hpp"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>

using namespace emerick::yamaha;

namespace {

// Offsets of the fields decoded from templates saved on the keyboard.
constexpr std::size_t kClock = 0x24;
constexpr std::size_t kStartStop = 0x27;
constexpr std::size_t kRhythm1Channel = 0x44;
constexpr std::size_t kRhythm2Channel = 0x46;
constexpr std::size_t kControllerPort = 0x100;
constexpr std::size_t kControllerChannel = 0x101;

std::size_t buttonSlot(int cc) {
    return static_cast<std::size_t>(cc == 0 ? 0x102 : cc < 8 ? 0x103 + (cc - 5) : 0x105 + (cc - 8));
}

}  // namespace

TEST_CASE("the keyboard template has the keyboard's header and size") {
    const auto bytes = encodeKeyboardTemplate({});
    REQUIRE(bytes.size() == 298);
    CHECK(std::string(bytes.begin(), bytes.begin() + 16) == "MIS-100-100-1000");
    CHECK(bytes[0x16] == 0x0F);
    CHECK(bytes[0x17] == 0xB9);
}

TEST_CASE("the keyboard template follows Emerick's clock and starts the Style on Start") {
    const auto bytes = encodeKeyboardTemplate({});
    CHECK(bytes[kClock] == 3);
    CHECK(bytes[kStartStop] == 1);
    for (std::size_t sysex = 0x28; sysex <= 0x2B; ++sysex) {
        CHECK(bytes[sysex] == 1);
    }
    const auto internal = encodeKeyboardTemplate({.clock = TemplatePort::Internal, .startStop = false});
    CHECK(internal[kClock] == 0);
    CHECK(internal[kStartStop] == 0);
}

TEST_CASE("the keyboard template sends every part on its default channel, Rhythm 1 and 2 included") {
    const auto bytes = encodeKeyboardTemplate({});
    CHECK(bytes[0x30] == 1);
    CHECK(bytes[0x32] == 2);
    CHECK(bytes[0x38] == 0);
    CHECK(bytes[0x3C] == 5);
    CHECK(bytes[kRhythm1Channel] == 9);
    CHECK(bytes[kRhythm2Channel] == 10);
    CHECK(bytes[0x72] == 32);
    CHECK(bytes[0x45] == 0xF8);
}

TEST_CASE("the keyboard template listens for Style buttons on USB2 channel 16") {
    const auto bytes = encodeKeyboardTemplate({});
    CHECK(bytes[kControllerPort] == 4);
    CHECK(bytes[kControllerChannel] == 15);
    CHECK(bytes[buttonSlot(0)] == static_cast<std::uint8_t>(ButtonFunction::StyleStartStop));
    CHECK(bytes[buttonSlot(5)] == static_cast<std::uint8_t>(ButtonFunction::FillDown));
    CHECK(bytes[buttonSlot(12)] == static_cast<std::uint8_t>(ButtonFunction::MainB));
    CHECK(bytes[buttonSlot(23)] == static_cast<std::uint8_t>(ButtonFunction::Intro3));
    CHECK(bytes[buttonSlot(30)] == static_cast<std::uint8_t>(ButtonFunction::SynchroStop));
    CHECK(bytes[buttonSlot(34)] == static_cast<std::uint8_t>(ButtonFunction::FadeInOut));

    const auto moved = encodeKeyboardTemplate(
        {.controllerPort = TemplatePort::Usb1, .controllerChannel = 1, .buttons = {{11, ButtonFunction::MainD}}});
    CHECK(moved[kControllerPort] == 3);
    CHECK(moved[kControllerChannel] == 0);
    CHECK(moved[buttonSlot(11)] == static_cast<std::uint8_t>(ButtonFunction::MainD));
    CHECK(moved[buttonSlot(0)] == 0);
}

TEST_CASE("the keyboard template refuses a channel or a CC the keyboard has no item for") {
    CHECK_THROWS_AS(encodeKeyboardTemplate({.controllerChannel = 17}), std::invalid_argument);
    CHECK_THROWS_AS(encodeKeyboardTemplate({.buttons = {{7, ButtonFunction::MainA}}}), std::invalid_argument);
}
