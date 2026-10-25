#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/model/document.hpp"

namespace emerick::model {

enum class ControlKind : std::uint8_t { controller, pitchBend, pressure };

// One continuous MIDI control of a clip's events: a controller number, pitch bend or channel pressure.
struct Control {
    ControlKind kind = ControlKind::controller;
    int number = 0;

    auto operator<=>(const Control&) const = default;
};

// A value from controlMinimum to controlMaximum; pitch bend is centered at zero.
struct ControlPoint {
    Ticks at = 0;
    int value = 0;

    bool operator==(const ControlPoint&) const = default;
};

// Ids are "pitchBend", "pressure" and "cc<number>", such as "cc64".
std::optional<Control> parseControl(std::string_view id);
std::string controlId(Control control);
int controlMinimum(Control control);
int controlMaximum(Control control);

std::optional<Control> controlOf(const MidiEvent& event);
// The controls the clip's events hold, in order.
std::vector<Control> controlsIn(const MidiClip& clip);
// The control's values on every channel in time order.
std::vector<ControlPoint> controlPoints(const MidiClip& clip, Control control);
// The channel of the control's first event, else of the first note or event, else 1.
int controlChannel(const MidiClip& clip, Control control);
// Removes the control's events from from to to, both included, and at each point's tick, then adds the points on
// channel. We drop a point that repeats the value of the point before it, since values hold until the next point.
void replaceControl(MidiClip& clip, Control control, Ticks from, Ticks to, std::span<const ControlPoint> points,
                    int channel);

}  // namespace emerick::model
