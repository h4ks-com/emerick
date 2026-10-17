#include "emerick/model/clip_edit.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <variant>
#include <vector>

#include "emerick/core/midi.hpp"
#include "emerick/core/overloaded.hpp"

namespace emerick::model {

namespace {

using core::midi::kNoteOff;
using core::midi::kNoteOn;

struct Note {
    std::uint8_t channel = 0;
    std::uint8_t key = 0;
    auto operator<=>(const Note&) const = default;
};

std::optional<Note> noteOf(const MidiEvent& event, bool on) {
    if (event.bytes.size() < 3) {
        return std::nullopt;
    }
    const auto type = static_cast<std::uint8_t>(event.bytes[0] & 0xF0);
    const bool isOn = type == kNoteOn && event.bytes[2] > 0;
    const bool isOff = type == kNoteOff || (type == kNoteOn && event.bytes[2] == 0);
    if ((on && !isOn) || (!on && !isOff)) {
        return std::nullopt;
    }
    return Note{static_cast<std::uint8_t>(event.bytes[0] & 0x0F), event.bytes[1]};
}

// Keeps the events in [from, to), moved so from becomes 0.
std::vector<MidiEvent> cutEvents(std::vector<MidiEvent> events, Ticks from, Ticks to) {
    std::ranges::stable_sort(events, {}, &MidiEvent::at);
    std::multiset<Note> held;
    std::vector<MidiEvent> kept;
    for (MidiEvent& event : events) {
        if (event.at < from || event.at >= to) {
            continue;
        }
        if (const auto on = noteOf(event, true)) {
            held.insert(*on);
        } else if (const auto off = noteOf(event, false)) {
            const auto found = held.find(*off);
            if (found == held.end()) {
                continue;
            }
            held.erase(found);
        }
        event.at -= from;
        kept.push_back(std::move(event));
    }
    for (const Note& note : held) {
        kept.push_back({to - from - 1, {static_cast<std::uint8_t>(kNoteOff | note.channel), note.key, 0}});
    }
    return kept;
}

std::string lengthError() { return "a clip needs a length above 0 ticks"; }

}  // namespace

Ticks clipStart(const Clip& clip) {
    return std::visit([](const auto& value) { return value.start; }, clip);
}

Ticks clipLength(const Clip& clip) {
    return std::visit([](const auto& value) { return value.length; }, clip);
}

Ticks clipEnd(const Clip& clip) { return clipStart(clip) + clipLength(clip); }

Clip moveClip(Clip clip, Ticks start) {
    std::visit([start](auto& value) { value.start = start; }, clip);
    return clip;
}

Clip duplicateClip(Clip clip, Ticks start) {
    std::visit([](auto& value) { value.id = ClipId::make(); }, clip);
    return moveClip(std::move(clip), start);
}

std::expected<Clip, std::string> trimStart(const Clip& clip, Ticks start, const core::TempoMap& tempo,
                                           double sampleRate) {
    if (start < 0) {
        return std::unexpected("a clip cannot start before bar 1");
    }
    const Ticks delta = start - clipStart(clip);
    const Ticks length = clipLength(clip) - delta;
    if (length <= 0) {
        return std::unexpected(lengthError());
    }
    return std::visit(
        core::Overloaded{[&](AudioClip audio) -> std::expected<Clip, std::string> {
                             const core::Frames offset = audio.sourceOffset + tempo.ticksToFrames(start, sampleRate) -
                                                         tempo.ticksToFrames(audio.start, sampleRate);
                             if (offset < 0) {
                                 return std::unexpected("the clip cannot start before its audio file starts");
                             }
                             audio.sourceOffset = offset;
                             audio.start = start;
                             audio.length = length;
                             return audio;
                         },
                         [&](MidiClip midi) -> std::expected<Clip, std::string> {
                             midi.events = cutEvents(std::move(midi.events), delta, midi.length);
                             midi.start = start;
                             midi.length = length;
                             return midi;
                         }},
        clip);
}

std::expected<Clip, std::string> setLength(const Clip& clip, Ticks length) {
    if (length <= 0) {
        return std::unexpected(lengthError());
    }
    return std::visit(core::Overloaded{[&](AudioClip audio) -> Clip {
                                           audio.length = length;
                                           return audio;
                                       },
                                       [&](MidiClip midi) -> Clip {
                                           midi.events = cutEvents(std::move(midi.events), 0, length);
                                           midi.length = length;
                                           return midi;
                                       }},
                      clip);
}

std::expected<std::pair<Clip, Clip>, std::string> split(const Clip& clip, Ticks at, const core::TempoMap& tempo,
                                                        double sampleRate) {
    const Ticks start = clipStart(clip);
    const Ticks end = clipEnd(clip);
    if (at <= start || at >= end) {
        return std::unexpected("split position must be inside the clip, between " + std::to_string(start) + " and " +
                               std::to_string(end) + " ticks");
    }
    auto left = setLength(clip, at - start);
    auto right = trimStart(duplicateClip(clip, start), at, tempo, sampleRate);
    if (!left || !right) {
        return std::unexpected(left ? right.error() : left.error());
    }
    std::visit(core::Overloaded{[](AudioClip& audio) { audio.fadeOut = 0; }, [](MidiClip&) {}}, *left);
    std::visit(core::Overloaded{[](AudioClip& audio) { audio.fadeIn = 0; }, [](MidiClip&) {}}, *right);
    return std::pair{std::move(*left), std::move(*right)};
}

}  // namespace emerick::model
