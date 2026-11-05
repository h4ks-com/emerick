#include "emerick/yamaha/keyboard_template.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace emerick::yamaha {

namespace {

constexpr std::size_t kFileSize = 0x12A;
constexpr std::string_view kMagic = "MIS-100-100-1000";
constexpr std::uint32_t kGenos2ModelId = 4025;

constexpr std::size_t kClock = 0x24;
constexpr std::size_t kStartStop = 0x27;
constexpr std::size_t kSysexSwitches = 0x28;
constexpr std::size_t kTransmit = 0x30;
constexpr std::size_t kReceive = 0x78;
constexpr std::size_t kController = 0x100;
constexpr std::size_t kButtons = 0x102;
constexpr std::size_t kKnobs = 0x120;

constexpr std::size_t kTransmitParts = 34;
constexpr std::size_t kChannels = 32;
constexpr std::uint8_t kAllMessages = 0xF8;
constexpr std::uint8_t kCutoff = 0x0E;
constexpr std::uint8_t kKeyboardPart = 0x04;

// The part each USB2 channel plays in a template saved on the keyboard; USB1 channels all take part 01.
constexpr std::array<std::uint8_t, 16> kUsb2Parts{0x05, 0x01, 0x02, 0x03, 0x04, 0x0E, 0x05, 0x10,
                                                  0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D};

// CC 1 to 4 and 7 are the continuous items, so the on/off items skip them.
constexpr std::array<int, 30> kButtonCcs{0,  5,  6,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                         20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34};

void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[at + index] = static_cast<std::uint8_t>(value >> (8 * (3 - index)));
    }
}

// Right 1, Left, Right 2 and Right 3 send on 1 to 4, Upper and Lower are off, then Multi Pads, Style parts and Song
// channels follow on 5 to 32, where 17 to 32 are USB2.
std::uint8_t transmitChannel(std::size_t part) {
    if (part < 4) {
        return static_cast<std::uint8_t>(part + 1);
    }
    return part < 6 ? 0 : static_cast<std::uint8_t>(part - 1);
}

}  // namespace

std::map<int, ButtonFunction> styleButtons() {
    using enum ButtonFunction;
    std::map<int, ButtonFunction> buttons{{11, MainA},   {12, MainB},   {13, MainC},   {14, MainD},
                                          {21, Intro1},  {22, Intro2},  {23, Intro3},  {24, TapTempo},
                                          {31, Ending1}, {32, Ending2}, {33, Ending3}, {34, FadeInOut}};
    for (const int first : {0, 15, 25}) {
        buttons[first] = StyleStartStop;
    }
    for (const int first : {5, 16, 26}) {
        buttons[first] = FillDown;
        buttons[first + 1] = FillSelf;
    }
    for (const int first : {8, 18, 28}) {
        buttons[first] = FillBreak;
        buttons[first + 1] = FillUp;
        buttons[first + 2] = SynchroStop;
    }
    return buttons;
}

std::vector<std::uint8_t> encodeKeyboardTemplate(const KeyboardTemplate& settings) {
    if (settings.controllerChannel < 1 || settings.controllerChannel > 16) {
        throw std::invalid_argument("the External Controller channel must be 1 to 16");
    }
    std::vector<std::uint8_t> bytes(kFileSize, 0);
    std::ranges::copy(kMagic, bytes.begin());
    put32(bytes, 0x10, 4);
    put32(bytes, 0x14, kGenos2ModelId);
    put32(bytes, 0x20, 0xFE);

    bytes[kClock] = static_cast<std::uint8_t>(settings.clock);
    bytes[kStartStop] = settings.startStop ? 1 : 0;
    // We turn on all four sysex switches, a combination a template saved on the keyboard holds.
    std::fill_n(bytes.begin() + kSysexSwitches, 4, 1);

    put32(bytes, kTransmit - 4, kTransmitParts);
    for (std::size_t part = 0; part < kTransmitParts; ++part) {
        bytes[kTransmit + (2 * part)] = transmitChannel(part);
        bytes[kTransmit + (2 * part) + 1] = kAllMessages;
    }
    put32(bytes, kReceive - 4, kChannels);
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        bytes[kReceive + (2 * channel)] = channel < 16 ? 0x01 : kUsb2Parts[channel - 16];
        bytes[kReceive + (2 * channel) + 1] = kAllMessages;
    }
    put32(bytes, 0xB8, kChannels);
    put32(bytes, 0xDC, kChannels);

    bytes[kController] = static_cast<std::uint8_t>(settings.controllerPort);
    bytes[kController + 1] = static_cast<std::uint8_t>(settings.controllerChannel - 1);
    for (const auto& [cc, function] : settings.buttons) {
        const auto item =
            static_cast<std::size_t>(std::ranges::distance(kButtonCcs.begin(), std::ranges::find(kButtonCcs, cc)));
        if (item == kButtonCcs.size()) {
            throw std::invalid_argument("the External Controller has no on/off item for CC " + std::to_string(cc));
        }
        bytes[kButtons + item] = static_cast<std::uint8_t>(function);
    }
    bytes[kKnobs] = kCutoff;
    bytes[kKnobs + 1] = kKeyboardPart;
    return bytes;
}

}  // namespace emerick::yamaha
