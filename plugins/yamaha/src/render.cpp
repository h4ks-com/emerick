#include "emerick/yamaha/render.hpp"

#include <algorithm>
#include <optional>
#include <ranges>

#include "emerick/core/midi.hpp"
#include "emerick/yamaha/sysex.hpp"

namespace emerick::yamaha {

namespace {

std::optional<std::uint8_t> button(const SectionEvent& section, std::optional<int> playingMain) {
    const auto variation = static_cast<std::uint8_t>(section.variation);
    if (section.kind == Section::Main && playingMain == section.variation) {
        return std::nullopt;
    }
    if (section.kind == Section::FillTo) {
        if (playingMain == section.variation - 1) {
            return kFillUpCc;
        }
        if (playingMain == section.variation + 1) {
            return kFillDownCc;
        }
        if (playingMain == section.variation) {
            return sectionInfo(Section::Fill).controllerCc;
        }
    }
    const SectionInfo& info = sectionInfo(section.kind);
    return static_cast<std::uint8_t>(info.controllerCc + (info.variations.empty() ? 0 : variation));
}

bool switchesAtBarLine(Section kind) {
    return kind == Section::Main || kind == Section::Intro || kind == Section::Ending;
}

}  // namespace

std::vector<RawEvent> chordControls(const std::vector<ChordEvent>& chords) {
    return chords | std::views::transform([](const ChordEvent& event) {
               return RawEvent{event.at, encodeChordControl(event.chord)};
           }) |
           std::ranges::to<std::vector>();
}

std::vector<RawEvent> sectionPresses(const std::vector<SectionEvent>& sections, int channel, core::Ticks leadTicks) {
    const auto status = core::midi::statusFor(core::midi::kControlChange, channel);
    std::vector<RawEvent> events;
    std::optional<int> playingMain;
    for (const SectionEvent& section : sections) {
        const auto pressed = button(section, playingMain);
        if (section.kind == Section::Main || section.kind == Section::FillTo) {
            playingMain = section.variation;
        } else if (section.kind == Section::Ending) {
            playingMain.reset();
        }
        if (!pressed) {
            continue;
        }
        const core::Ticks at =
            switchesAtBarLine(section.kind) ? std::max<core::Ticks>(0, section.at - leadTicks) : section.at;
        events.push_back({at, {status, *pressed, kButtonDown}});
        events.push_back({at + kButtonReleaseTicks, {status, *pressed, 0}});
    }
    std::ranges::stable_sort(events, {}, &RawEvent::at);
    return events;
}

}  // namespace emerick::yamaha
