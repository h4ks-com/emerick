#include "emerick/model/controls.hpp"

#include <algorithm>
#include <charconv>
#include <ranges>
#include <utility>

#include "emerick/core/midi.hpp"

namespace emerick::model {

namespace {

constexpr int kMaxDataByte = 127;
constexpr int kBendCenter = 8192;
constexpr int kDataBits = 7;
constexpr std::string_view kControllerPrefix = "cc";
constexpr std::string_view kPitchBendId = "pitchBend";
constexpr std::string_view kPressureId = "pressure";

int channelOf(const MidiEvent& event) { return (event.bytes.at(0) & 0x0F) + 1; }

int valueOf(const MidiEvent& event, Control control) {
    switch (control.kind) {
        case ControlKind::controller:
            return event.bytes.at(2);
        case ControlKind::pitchBend:
            return (event.bytes.at(2) << kDataBits | event.bytes.at(1)) - kBendCenter;
        case ControlKind::pressure:
            return event.bytes.at(1);
    }
    return 0;
}

std::vector<std::uint8_t> bytesOf(Control control, int value, int channel) {
    using namespace core::midi;
    switch (control.kind) {
        case ControlKind::controller:
            return {statusFor(kControlChange, channel), static_cast<std::uint8_t>(control.number),
                    static_cast<std::uint8_t>(value)};
        case ControlKind::pitchBend: {
            const int raw = value + kBendCenter;
            return {statusFor(kPitchBend, channel), static_cast<std::uint8_t>(raw & kMaxDataByte),
                    static_cast<std::uint8_t>(raw >> kDataBits)};
        }
        case ControlKind::pressure:
            return {statusFor(kChannelPressure, channel), static_cast<std::uint8_t>(value)};
    }
    return {};
}

}  // namespace

std::optional<Control> parseControl(std::string_view id) {
    if (id == kPitchBendId) {
        return Control{.kind = ControlKind::pitchBend};
    }
    if (id == kPressureId) {
        return Control{.kind = ControlKind::pressure};
    }
    if (!id.starts_with(kControllerPrefix)) {
        return std::nullopt;
    }
    const std::string_view digits = id.substr(kControllerPrefix.size());
    int number = -1;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (error != std::errc{} || end != digits.data() + digits.size() || number < 0 || number > kMaxDataByte) {
        return std::nullopt;
    }
    return Control{.kind = ControlKind::controller, .number = number};
}

std::string controlId(Control control) {
    switch (control.kind) {
        case ControlKind::controller:
            return std::string{kControllerPrefix} + std::to_string(control.number);
        case ControlKind::pitchBend:
            return std::string{kPitchBendId};
        case ControlKind::pressure:
            return std::string{kPressureId};
    }
    return {};
}

int controlMinimum(Control control) { return control.kind == ControlKind::pitchBend ? -kBendCenter : 0; }

int controlMaximum(Control control) { return control.kind == ControlKind::pitchBend ? kBendCenter - 1 : kMaxDataByte; }

std::optional<Control> controlOf(const MidiEvent& event) {
    if (event.bytes.empty()) {
        return std::nullopt;
    }
    const auto type = static_cast<std::uint8_t>(event.bytes[0] & 0xF0);
    if (type == core::midi::kControlChange && event.bytes.size() >= 3) {
        return Control{.kind = ControlKind::controller, .number = event.bytes[1]};
    }
    if (type == core::midi::kPitchBend && event.bytes.size() >= 3) {
        return Control{.kind = ControlKind::pitchBend};
    }
    if (type == core::midi::kChannelPressure && event.bytes.size() >= 2) {
        return Control{.kind = ControlKind::pressure};
    }
    return std::nullopt;
}

std::vector<Control> controlsIn(const MidiClip& clip) {
    auto controls = clip.events | std::views::transform(controlOf) |
                    std::views::filter([](const auto& control) { return control.has_value(); }) |
                    std::views::transform([](const auto& control) { return *control; }) |
                    std::ranges::to<std::vector>();
    std::ranges::sort(controls);
    const auto [first, last] = std::ranges::unique(controls);
    controls.erase(first, last);
    return controls;
}

std::vector<ControlPoint> controlPoints(const MidiClip& clip, Control control) {
    auto points = clip.events |
                  std::views::filter([&](const MidiEvent& event) { return controlOf(event) == control; }) |
                  std::views::transform([&](const MidiEvent& event) {
                      return ControlPoint{.at = event.at, .value = valueOf(event, control)};
                  }) |
                  std::ranges::to<std::vector>();
    std::ranges::stable_sort(points, {}, &ControlPoint::at);
    return points;
}

int controlChannel(const MidiClip& clip, Control control) {
    const auto own = std::ranges::find(clip.events, std::optional{control}, controlOf);
    if (own != clip.events.end()) {
        return channelOf(*own);
    }
    if (!clip.notes.empty()) {
        return clip.notes.front().channel;
    }
    const auto any = std::ranges::find_if(clip.events, [](const MidiEvent& event) {
        return !event.bytes.empty() && core::midi::isChannelMessage(event.bytes[0]);
    });
    return any == clip.events.end() ? 1 : channelOf(*any);
}

void replaceControl(MidiClip& clip, Control control, Ticks from, Ticks to, std::span<const ControlPoint> points,
                    int channel) {
    std::vector<ControlPoint> added{points.begin(), points.end()};
    std::ranges::stable_sort(added, {}, &ControlPoint::at);
    std::erase_if(clip.events, [&](const MidiEvent& event) {
        return controlOf(event) == control &&
               ((event.at >= from && event.at <= to) || std::ranges::contains(added, event.at, &ControlPoint::at));
    });
    std::optional<int> last;
    for (const ControlPoint& point : added) {
        if (point.value != last) {
            clip.events.push_back({.at = point.at, .bytes = bytesOf(control, point.value, channel)});
        }
        last = point.value;
    }
    std::ranges::stable_sort(clip.events, {}, &MidiEvent::at);
}

}  // namespace emerick::model
