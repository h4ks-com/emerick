#include "emerick/model/controls.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace emerick;
using namespace emerick::model;

namespace {

constexpr Control kModulation{.kind = ControlKind::controller, .number = 1};
constexpr Control kBend{.kind = ControlKind::pitchBend};
constexpr Control kPressure{.kind = ControlKind::pressure};

MidiClip played() {
    MidiClip clip{.length = 1920};
    clip.events = {{0, {0xC2, 5}},      {10, {0xB2, 1, 20}},      {20, {0xE2, 0x00, 0x40}}, {30, {0xD2, 90}},
                   {40, {0xB2, 1, 60}}, {50, {0xE2, 0x7F, 0x7F}}, {60, {0xB2, 64, 127}},    {70, {0xE2, 0x00, 0x00}}};
    return clip;
}

}  // namespace

TEST_CASE("control ids read back as the control they name and refuse anything else") {
    CHECK(parseControl("cc64") == Control{.kind = ControlKind::controller, .number = 64});
    CHECK(parseControl("pitchBend") == kBend);
    CHECK(parseControl("pressure") == kPressure);
    for (const char* bad : {"cc", "cc128", "cc-1", "cc1x", "velocity", ""}) {
        CHECK_FALSE(parseControl(bad).has_value());
    }
    for (const Control control : {kModulation, kBend, kPressure}) {
        CHECK(parseControl(controlId(control)) == control);
    }
}

TEST_CASE("a clip lists the controls its events hold and their values, with pitch bend centered at zero") {
    const MidiClip clip = played();
    CHECK(controlsIn(clip) ==
          std::vector<Control>{kModulation, {.kind = ControlKind::controller, .number = 64}, kBend, kPressure});
    CHECK(controlPoints(clip, kModulation) == std::vector<ControlPoint>{{10, 20}, {40, 60}});
    CHECK(controlPoints(clip, kBend) == std::vector<ControlPoint>{{20, 0}, {50, 8191}, {70, -8192}});
    CHECK(controlPoints(clip, kPressure) == std::vector<ControlPoint>{{30, 90}});
    CHECK(controlChannel(clip, kBend) == 3);
    CHECK(controlChannel(MidiClip{}, kBend) == 1);
}

TEST_CASE("replacing a range keeps the other events and drops points that repeat the value before them") {
    MidiClip clip = played();
    const std::vector<ControlPoint> drawn{{45, 7}, {15, 7}, {25, 7}, {35, 100}};
    replaceControl(clip, kModulation, 10, 30, drawn, 3);
    CHECK(controlPoints(clip, kModulation) == std::vector<ControlPoint>{{15, 7}, {35, 100}, {40, 60}, {45, 7}});
    CHECK(clip.events.size() == played().events.size() + 2);
    CHECK(std::ranges::is_sorted(clip.events, {}, &MidiEvent::at));
    CHECK(std::ranges::find(clip.events, 15, &MidiEvent::at)->bytes == std::vector<std::uint8_t>{0xB2, 1, 7});

    replaceControl(clip, kBend, 0, 1919, std::vector<ControlPoint>{{100, -8192}}, 3);
    CHECK(std::ranges::find(clip.events, 100, &MidiEvent::at)->bytes == std::vector<std::uint8_t>{0xE2, 0, 0});
    CHECK(controlPoints(clip, kBend).size() == 1);

    replaceControl(clip, kPressure, 0, 1919, {}, 3);
    CHECK(controlPoints(clip, kPressure).empty());
    CHECK(std::ranges::find(clip.events, 0, &MidiEvent::at)->bytes == std::vector<std::uint8_t>{0xC2, 5});
}

TEST_CASE("moving a point removes it where it was and replaces a point at the tick it lands on") {
    MidiClip clip = played();
    replaceControl(clip, kModulation, 10, 10, std::vector<ControlPoint>{{40, 33}}, 3);
    CHECK(controlPoints(clip, kModulation) == std::vector<ControlPoint>{{40, 33}});
}
