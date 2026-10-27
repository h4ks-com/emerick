#include "emerick/model/midi_notes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <ranges>
#include <utility>

#include "emerick/core/midi.hpp"

namespace emerick::model {

namespace {

using core::midi::kNoteOff;
using core::midi::kNoteOn;

struct Key {
    int channel = 1;
    int pitch = 0;
    auto operator<=>(const Key&) const = default;
};

std::optional<Key> keyOf(const MidiEvent& event, bool on) {
    if (event.bytes.size() < 3) {
        return std::nullopt;
    }
    const auto type = static_cast<std::uint8_t>(event.bytes[0] & 0xF0);
    const bool isOn = type == kNoteOn && event.bytes[2] > 0;
    const bool isOff = type == kNoteOff || (type == kNoteOn && event.bytes[2] == 0);
    if ((on && !isOn) || (!on && !isOff)) {
        return std::nullopt;
    }
    return Key{(event.bytes[0] & 0x0F) + 1, event.bytes[1]};
}

// Calls repeat(offset, end) for each pass through the content, where end is how much of the content that pass plays.
template <class Repeat>
void forEachRepeat(const MidiClip& clip, Repeat repeat) {
    if (clip.length <= 0) {
        return;
    }
    const Ticks span = clipSpan(clip);
    for (Ticks offset = 0; offset < span; offset += clip.length) {
        repeat(offset, std::min(clip.length, span - offset));
    }
}

auto eventBefore(Ticks end) {
    return [end](const MidiEvent& event) { return event.at >= 0 && event.at < end; };
}

auto noteBefore(Ticks end) {
    return [end](const Note& note) { return note.start >= 0 && note.start < end; };
}

}  // namespace

void gatherNotes(MidiClip& clip) {
    std::ranges::stable_sort(clip.events, {}, &MidiEvent::at);
    std::multimap<Key, Note> held;
    std::vector<MidiEvent> others;
    for (MidiEvent& event : clip.events) {
        if (const auto on = keyOf(event, true)) {
            held.emplace(*on, Note{.pitch = on->pitch,
                                   .velocity = event.bytes[2],
                                   .start = event.at,
                                   .length = 0,
                                   .channel = on->channel});
        } else if (const auto off = keyOf(event, false)) {
            if (const auto found = held.find(*off); found != held.end()) {
                Note note = found->second;
                note.length = std::max<Ticks>(1, event.at - note.start);
                clip.notes.push_back(note);
                held.erase(found);
            }
        } else {
            others.push_back(std::move(event));
        }
    }
    for (auto& [key, note] : held) {
        note.length = std::max<Ticks>(1, clip.length - note.start);
        clip.notes.push_back(note);
    }
    std::ranges::stable_sort(clip.notes, {}, &Note::start);
    clip.events = std::move(others);
}

bool looped(const MidiClip& clip) { return clip.length > 0 && clip.loopLength > clip.length; }

Ticks clipSpan(const MidiClip& clip) { return looped(clip) ? clip.loopLength : clip.length; }

MidiClip unrolled(const MidiClip& clip) {
    MidiClip result = clip;
    result.length = clipSpan(clip);
    result.loopLength = 0;
    result.events.clear();
    result.notes.clear();
    forEachRepeat(clip, [&](Ticks offset, Ticks end) {
        for (const MidiEvent& event : clip.events | std::views::filter(eventBefore(end))) {
            result.events.push_back({offset + event.at, event.bytes});
        }
        for (const Note& note : clip.notes | std::views::filter(noteBefore(end))) {
            Note copy = note;
            copy.id = offset == 0 ? note.id : NoteId::make();
            copy.start += offset;
            copy.length = std::min(note.start + note.length, end) - note.start;
            result.notes.push_back(copy);
        }
    });
    return result;
}

std::vector<MidiEvent> clipEvents(const MidiClip& clip) {
    struct Placed {
        int rank;
        MidiEvent event;
    };
    constexpr int kOffRank = 0;
    constexpr int kOtherRank = 1;
    constexpr int kOnRank = 2;
    std::vector<Placed> placed;
    forEachRepeat(clip, [&](Ticks offset, Ticks end) {
        for (const MidiEvent& event : clip.events | std::views::filter(eventBefore(end))) {
            placed.push_back({kOtherRank, {offset + event.at, event.bytes}});
        }
        for (const Note& note : clip.notes | std::views::filter(noteBefore(end))) {
            const auto key = static_cast<std::uint8_t>(note.pitch);
            placed.push_back(
                {kOnRank,
                 {offset + note.start,
                  {core::midi::statusFor(kNoteOn, note.channel), key, static_cast<std::uint8_t>(note.velocity)}}});
            placed.push_back({kOffRank,
                              {offset + std::min(note.start + note.length, end),
                               {core::midi::statusFor(kNoteOff, note.channel), key, 0}}});
        }
    });
    std::ranges::stable_sort(placed, {}, [](const Placed& entry) { return std::pair{entry.event.at, entry.rank}; });
    return placed | std::views::transform(&Placed::event) | std::ranges::to<std::vector>();
}

Ticks quantized(Ticks at, Ticks grid, double strength) {
    if (grid <= 0) {
        return at;
    }
    const Ticks nearest = (at + grid / 2) / grid * grid;
    return at + static_cast<Ticks>(std::llround(static_cast<double>(nearest - at) * std::clamp(strength, 0.0, 1.0)));
}

}  // namespace emerick::model
