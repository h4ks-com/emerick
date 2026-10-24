#include "emerick/yamaha/render.hpp"

#include <algorithm>
#include <ranges>

#include "emerick/yamaha/sysex.hpp"

namespace emerick::yamaha {

std::vector<RawEvent> chordControls(const std::vector<ChordEvent>& chords) {
    return chords | std::views::transform([](const ChordEvent& event) {
               return RawEvent{event.at, encodeChordControl(event.chord)};
           }) |
           std::ranges::to<std::vector>();
}

std::vector<RawEvent> sectionControls(const std::vector<SectionEvent>& sections) {
    std::vector<RawEvent> events;
    int playingMain = 0;
    for (const SectionEvent& section : sections) {
        SectionPress press{section.kind, section.variation, true};
        if (section.kind == Section::Main || section.kind == Section::FillTo) {
            playingMain = section.variation;
        } else if (section.kind == Section::Fill) {
            press = {Section::FillTo, playingMain, true};
        }
        events.push_back({section.at, encodeSectionControl(press)});
        press.on = false;
        events.push_back({section.at + kButtonReleaseTicks, encodeSectionControl(press)});
    }
    std::ranges::stable_sort(events, {}, &RawEvent::at);
    return events;
}

}  // namespace emerick::yamaha
