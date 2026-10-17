#include "emerick/yamaha/render.hpp"

#include <algorithm>

#include "emerick/core/midi.hpp"

namespace emerick::yamaha {

namespace {

using core::midi::kControlChange;
using core::midi::kNoteOff;
using core::midi::kNoteOn;
using core::midi::statusFor;

constexpr int kBaseOctaveNote = 48;
constexpr std::uint8_t kVelocity = 90;
constexpr std::uint8_t kButtonOn = 127;

// A release sorts before a press at the same tick, so a repeated note or button restarts cleanly.
struct Ordered {
    bool press = false;
    RawEvent event;
};

std::vector<RawEvent> inTimeOrder(std::vector<Ordered> events) {
    std::ranges::stable_sort(events, [](const Ordered& a, const Ordered& b) {
        return a.event.at != b.event.at ? a.event.at < b.event.at : a.press < b.press;
    });
    std::vector<RawEvent> result;
    result.reserve(events.size());
    for (auto& ordered : events) {
        result.push_back(std::move(ordered.event));
    }
    return result;
}

}  // namespace

std::vector<RawEvent> chordNotes(const std::vector<ChordEvent>& chords, int channel, core::Ticks endTick) {
    std::vector<Ordered> events;
    for (std::size_t i = 0; i < chords.size(); ++i) {
        const core::Ticks until = i + 1 < chords.size() ? chords[i + 1].at : endTick;
        for (const int note : chords[i].chord.notes(kBaseOctaveNote)) {
            const auto key = static_cast<std::uint8_t>(note);
            events.push_back({true, {chords[i].at, {statusFor(kNoteOn, channel), key, kVelocity}}});
            events.push_back({false, {until, {statusFor(kNoteOff, channel), key, 0}}});
        }
    }
    return inTimeOrder(std::move(events));
}

int sectionCc(const SectionEvent& event) {
    const SectionInfo& info = sectionInfo(event.kind);
    return info.ccBase + (info.variations.empty() ? 0 : event.variation);
}

std::vector<RawEvent> sectionControls(const std::vector<SectionEvent>& sections, int channel) {
    std::vector<Ordered> events;
    for (const auto& section : sections) {
        const auto cc = static_cast<std::uint8_t>(sectionCc(section));
        events.push_back({true, {section.at, {statusFor(kControlChange, channel), cc, kButtonOn}}});
        events.push_back({false, {section.at + kButtonReleaseTicks, {statusFor(kControlChange, channel), cc, 0}}});
    }
    return inTimeOrder(std::move(events));
}

}  // namespace emerick::yamaha
