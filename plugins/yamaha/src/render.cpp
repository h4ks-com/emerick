#include "emerick/yamaha/render.hpp"

#include <algorithm>
#include <iterator>
#include <optional>
#include <ranges>

#include "emerick/core/midi.hpp"
#include "emerick/yamaha/sysex.hpp"

namespace emerick::yamaha {

namespace {

std::uint8_t pressFor(const SectionEvent& section, std::optional<int> playingMain) {
    const auto variation = static_cast<std::uint8_t>(section.variation);
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

std::optional<std::uint8_t> button(const SectionEvent& section, std::optional<int> playingMain) {
    if (section.kind == Section::Main && playingMain == section.variation) {
        return std::nullopt;
    }
    return pressFor(section, playingMain);
}

}  // namespace

std::vector<RawEvent> chordControls(const std::vector<ChordEvent>& chords) {
    return chords | std::views::transform([](const ChordEvent& event) {
               return RawEvent{event.at, encodeChordControl(event.chord)};
           }) |
           std::ranges::to<std::vector>();
}

std::vector<RawEvent> sectionPresses(const std::vector<SectionEvent>& sections, int channel, core::Ticks leadTicks) {
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
            startsSection(section.kind) ? std::max<core::Ticks>(0, section.at - leadTicks) : section.at;
        auto press = buttonPress(*pressed, channel);
        events.push_back({at, std::move(press.front())});
        events.push_back({at + kButtonReleaseTicks, std::move(press.back())});
    }
    std::ranges::stable_sort(events, {}, &RawEvent::at);
    return events;
}

std::vector<std::vector<std::uint8_t>> buttonPress(std::uint8_t cc, int channel) {
    const auto status = core::midi::statusFor(core::midi::kControlChange, channel);
    return {{status, cc, kButtonDown}, {status, cc, 0}};
}

std::vector<std::vector<std::uint8_t>> sectionPress(const SectionEvent& section, int channel) {
    return buttonPress(pressFor(section, std::nullopt), channel);
}

std::optional<SectionEvent> chaseSection(const std::vector<SectionEvent>& sections, core::Ticks position) {
    const auto after = std::ranges::upper_bound(sections, position, {}, &SectionEvent::at);
    if (after == sections.begin()) {
        return std::nullopt;
    }
    const SectionEvent& current = *std::prev(after);
    switch (current.kind) {
        case Section::Main:
        case Section::Intro:
        case Section::Ending:
            return current;
        case Section::FillTo:
            return SectionEvent{current.at, Section::Main, current.variation};
        case Section::Fill:
        case Section::Break:
            break;
    }
    for (auto earlier = std::prev(after); earlier != sections.begin();) {
        --earlier;
        if (earlier->kind == Section::Main || earlier->kind == Section::FillTo) {
            return SectionEvent{current.at, Section::Main, earlier->variation};
        }
    }
    return std::nullopt;
}

std::optional<PatternPosition> patternAt(const std::vector<SectionEvent>& sections, core::Ticks position,
                                         core::Ticks ticksPerBar) {
    const auto after = std::ranges::upper_bound(sections, position, {}, &SectionEvent::at);
    const auto anchor = std::find_if(std::make_reverse_iterator(after), sections.rend(),
                                     [](const SectionEvent& event) { return startsSection(event.kind); });
    if (anchor == sections.rend()) {
        return std::nullopt;
    }
    const SectionEvent& section = *anchor;
    const core::Ticks start = section.at - (section.at % ticksPerBar);
    const int bars = section.kind == Section::Main ? std::max(1, section.bars) : 0;
    const core::Ticks pass = std::max<core::Ticks>(1, bars) * ticksPerBar;
    const core::Ticks passStart = bars == 0 ? start : start + ((position - start) / pass) * pass;
    return PatternPosition{.section = section,
                           .passStart = passStart,
                           .bar = static_cast<int>((position - passStart) / ticksPerBar) + 1,
                           .bars = bars};
}

}  // namespace emerick::yamaha
