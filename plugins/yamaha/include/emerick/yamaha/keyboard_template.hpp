#pragma once

#include <cstdint>
#include <map>
#include <vector>

namespace emerick::yamaha {

// The port codes a MIDI template uses for Clock and for the External Controller.
enum class TemplatePort : std::uint8_t { Internal = 0, MidiA = 1, MidiB = 2, Usb1 = 3, Usb2 = 4 };

// External Controller on/off functions, numbered in the order the Reference Manual lists them.
enum class ButtonFunction : std::uint8_t {
    None = 0x00,
    StyleStartStop = 0x14,
    TapTempo = 0x15,
    SynchroStop = 0x17,
    Intro1 = 0x18,
    Intro2 = 0x19,
    Intro3 = 0x1A,
    MainA = 0x1B,
    MainB = 0x1C,
    MainC = 0x1D,
    MainD = 0x1E,
    FillDown = 0x1F,
    FillSelf = 0x20,
    FillBreak = 0x21,
    FillUp = 0x22,
    Ending1 = 0x23,
    Ending2 = 0x24,
    Ending3 = 0x25,
    FadeInOut = 0x26,
};

// The Style buttons a CC number presses, as every template made on the keyboard so far assigns them.
std::map<int, ButtonFunction> styleButtons();

// The settings of a Genos2 MIDI template (.mis) that Emerick sets. The rest of the file keeps the layout of a
// template saved on the keyboard: every part transmits on its default channel and Rhythm 1 and 2 are on.
struct KeyboardTemplate {
    TemplatePort clock = TemplatePort::Usb1;
    bool startStop = true;
    TemplatePort controllerPort = TemplatePort::Usb2;
    int controllerChannel = 16;
    std::map<int, ButtonFunction> buttons = styleButtons();
};

// Throws std::invalid_argument for a channel outside 1 to 16 or a CC the External Controller has no on/off item for.
std::vector<std::uint8_t> encodeKeyboardTemplate(const KeyboardTemplate& settings);

}  // namespace emerick::yamaha
