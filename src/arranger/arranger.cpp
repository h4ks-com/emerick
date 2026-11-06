#include "emerick/arranger/arranger.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "emerick/arranger/style.hpp"
#include "emerick/arranger/transpose.hpp"
#include "emerick/core/midi.hpp"
#include "emerick/core/time.hpp"
#include "emerick/yamaha/chord.hpp"
#include "emerick/yamaha/section.hpp"

namespace emerick::arranger {

namespace {

namespace midi = core::midi;
using core::Ticks;
using yamaha::Section;

constexpr int kQuartersPerWhole = 4;

Ticks floorDiv(Ticks value, Ticks unit) { return value >= 0 ? value / unit : -((-value + unit - 1) / unit); }

std::uint8_t onChannel(std::uint8_t type, int channel) { return static_cast<std::uint8_t>(type | (channel & 0x0F)); }

}  // namespace

Arranger::Arranger(const Style& style, std::size_t capacity)
    : style_(&style),
      bar_(core::kTicksPerQuarter * kQuartersPerWhole * style.meter.numerator / style.meter.denominator),
      beat_(core::kTicksPerQuarter * kQuartersPerWhole / style.meter.denominator) {
    chord_.type = yamaha::kNoChordType;
    pending_.reserve(capacity);
    out_.reserve(capacity);
}

void Arranger::setChord(const yamaha::Chord& chord, Ticks at) {
    // We copy no note names, so a chord change never allocates.
    chord_.root.pitchClass = chord.root.pitchClass;
    chord_.type = chord.type;
    chord_.bass = chord.bass.transform(
        [](const yamaha::Note& bass) { return yamaha::Note{.pitchClass = bass.pitchClass, .name = {}}; });
    if (current_ == nullptr) {
        return;
    }
    for (int channel = 0; channel < kChannels; ++channel) {
        const auto& rule = current_->rules.at(static_cast<std::size_t>(channel));
        auto& notes = held_.at(static_cast<std::size_t>(channel));
        for (int source = 0; std::cmp_less(source, notes.size()); ++source) {
            Held& held = notes.at(static_cast<std::size_t>(source));
            if (!held.on) {
                continue;
            }
            const bool megaVoice = bankMsb_.at(static_cast<std::size_t>(channel)) == kMegaVoiceBankMsb;
            const auto moved = rule ? retriggerNote(*rule, source, held.note, chord_, megaVoice) : std::nullopt;
            if (moved && std::cmp_equal(*moved, held.note)) {
                continue;
            }
            push(pending_, {at, {onChannel(midi::kNoteOff, held.destination), held.note, 0}});
            held.on = moved.has_value();
            if (moved) {
                held.note = static_cast<std::uint8_t>(*moved);
                push(pending_, {at, {onChannel(midi::kNoteOn, held.destination), held.note, held.velocity}});
            }
        }
    }
}

void Arranger::request(Section kind, int variation, Ticks at) {
    const StyleSection* target = requestedSection(*style_, kind, variation);
    switch (kind) {
        case Section::Main:
            requestMain(target, at);
            return;
        case Section::Fill:
            if (const auto* fill =
                    onMain() ? findSection(*style_, Section::FillTo, playingMain_->variation) : nullptr) {
                startFill(fill, playingMain_, at);
            }
            return;
        case Section::FillTo: {
            const StyleSection* main = findSection(*style_, Section::Main, variation);
            if (main == nullptr) {
                return;
            }
            if (!onMain()) {
                requestMain(main, at);
            } else if (target != nullptr) {
                startFill(target, main, at);
            } else {
                switchAtBar(main, at);
            }
            return;
        }
        case Section::Break:
            if (onMain() && target != nullptr) {
                startFill(target, playingMain_, at);
            }
            return;
        case Section::Intro:
        case Section::Ending:
            if (target == nullptr) {
                return;
            }
            if (current_ == nullptr) {
                first_ = target;
            } else {
                landing_ = playingMain_;
                switchAtBar(target, at);
            }
            return;
    }
}

void Arranger::requestMain(const StyleSection* main, Ticks at) {
    if (main == nullptr) {
        return;
    }
    if (current_ == nullptr) {
        if (first_ == nullptr || first_->kind != Section::Intro) {
            first_ = main;
        }
        landing_ = main;
        return;
    }
    if (current_->kind == Section::Ending) {
        switchAtBar(main, at);
        return;
    }
    if (current_->kind != Section::Main) {
        landing_ = main;
        return;
    }
    const StyleSection* fill = autoFill_ ? findSection(*style_, Section::FillTo, main->variation) : nullptr;
    if (fill != nullptr) {
        startFill(fill, main, at);
    } else if (main != current_) {
        switchAtBar(main, at);
    }
}

void Arranger::startFill(const StyleSection* fill, const StyleSection* landing, Ticks at) {
    const Ticks beat = gridAfter(at, beat_);
    next_ = Switch{.section = fill, .at = beat, .start = gridBefore(beat, bar_)};
    landing_ = landing;
}

void Arranger::switchAtBar(const StyleSection* section, Ticks at) {
    const Ticks bar = gridAfter(at, bar_);
    next_ = Switch{.section = section, .at = bar, .start = bar};
}

void Arranger::start(Ticks at) {
    if (playing()) {
        return;
    }
    origin_ = at;
    next_.reset();
    const StyleSection* main = playingMain_ != nullptr ? playingMain_ : fallbackMain();
    const StyleSection* first = first_ != nullptr ? first_ : main;
    if (first == nullptr) {
        first = &style_->sections.front();
    }
    if (landing_ == nullptr) {
        landing_ = main;
    }
    first_ = nullptr;
    sendVoices(at);
    enter(first, at);
}

void Arranger::sendVoices(Ticks at) {
    for (int part = 0; part < kParts; ++part) {
        const auto& voice = style_->voices.at(static_cast<std::size_t>(part));
        if (!voice) {
            continue;
        }
        const int channel = kFirstPartChannel + part;
        for (const auto& message : voice->setup) {
            push(pending_,
                 {at, {onChannel(static_cast<std::uint8_t>(message[0] & 0xF0), channel), message[1], message[2]}});
        }
        bankMsb_.at(static_cast<std::size_t>(channel)) = voice->bankMsb;
    }
}

void Arranger::stop(Ticks at) {
    releaseAll(at, pending_);
    current_ = nullptr;
    next_.reset();
}

void Arranger::forget() {
    first_ = nullptr;
    landing_ = nullptr;
    playingMain_ = nullptr;
    next_.reset();
}

std::span<const MidiEvent> Arranger::render(Ticks from, Ticks to) {
    out_.clear();
    for (const MidiEvent& event : pending_) {
        push(out_, event);
    }
    pending_.clear();
    Ticks at = from;
    while (current_ != nullptr && at < to) {
        const Ticks boundary = boundaryAfter(at);
        if (boundary <= at) {
            advance(at);
            continue;
        }
        const Ticks end = std::min(boundary, to);
        playRange(at, end);
        at = end;
        if (at == boundary) {
            advance(at);
        }
    }
    return out_;
}

Ticks Arranger::length(const StyleSection& section) const { return std::max<Ticks>(1, hostTicks(section.length)); }

Ticks Arranger::hostTicks(std::int64_t styleTicks) const {
    return styleTicks * core::kTicksPerQuarter / style_->ticksPerQuarter;
}

Ticks Arranger::gridAfter(Ticks at, Ticks unit) const { return origin_ - floorDiv(origin_ - at, unit) * unit; }

Ticks Arranger::gridBefore(Ticks at, Ticks unit) const { return origin_ + floorDiv(at - origin_, unit) * unit; }

Ticks Arranger::boundaryAfter(Ticks at) const {
    const Ticks span = length(*current_);
    const Ticks end = current_->kind == Section::Main ? currentStart_ + (floorDiv(at - currentStart_, span) + 1) * span
                                                      : currentStart_ + span;
    return next_ ? std::min(next_->at, end) : end;
}

bool Arranger::onMain() const { return current_ != nullptr && current_->kind == Section::Main; }

const StyleSection* Arranger::fallbackMain() const {
    for (int variation = 0; variation < yamaha::variationCount(yamaha::sectionInfo(Section::Main)); ++variation) {
        if (const StyleSection* main = findSection(*style_, Section::Main, variation)) {
            return main;
        }
    }
    return nullptr;
}

void Arranger::enter(const StyleSection* section, Ticks start) {
    current_ = section;
    currentStart_ = start;
    if (section != nullptr && section->kind == Section::Main) {
        playingMain_ = section;
    }
}

void Arranger::advance(Ticks at) {
    releaseAll(at, out_);
    if (next_ && next_->at <= at) {
        const Switch next = *next_;
        next_.reset();
        enter(next.section, next.start);
        return;
    }
    switch (current_->kind) {
        case Section::Main:
            return;
        case Section::Ending:
            enter(nullptr, at);
            return;
        case Section::Fill:
        case Section::FillTo:
        case Section::Break:
        case Section::Intro: {
            const StyleSection* main = landing_ != nullptr ? landing_ : playingMain_;
            enter(main != nullptr ? main : fallbackMain(), at);
            return;
        }
    }
}

void Arranger::playRange(Ticks from, Ticks to) {
    const Ticks span = length(*current_);
    const Ticks passStart =
        current_->kind == Section::Main ? currentStart_ + floorDiv(from - currentStart_, span) * span : currentStart_;
    const auto& events = current_->events;
    auto event = std::ranges::lower_bound(events, from - passStart, {},
                                          [&](const StyleEvent& item) { return hostTicks(item.tick); });
    for (; event != events.end() && passStart + hostTicks(event->tick) < to; ++event) {
        play(*event, passStart + hostTicks(event->tick));
    }
}

void Arranger::play(const StyleEvent& event, Ticks at) {
    const int channel = event.bytes[0] & 0x0F;
    const auto& rule = current_->rules.at(static_cast<std::size_t>(channel));
    if (!rule) {
        return;
    }
    const auto type = static_cast<std::uint8_t>(event.bytes[0] & 0xF0);
    const std::uint8_t status = onChannel(type, rule->destination);
    switch (type) {
        case midi::kNoteOn:
            if (event.bytes[2] > 0) {
                noteOn(channel, *rule, event.bytes[1], event.bytes[2], at);
            } else {
                noteOff(channel, event.bytes[1], at);
            }
            return;
        case midi::kNoteOff:
            noteOff(channel, event.bytes[1], at);
            return;
        case midi::kControlChange:
            if (event.bytes[1] == midi::kBankSelectCc) {
                bankMsb_.at(static_cast<std::size_t>(channel)) = event.bytes[2];
            }
            push(out_, {at, {status, event.bytes[1], event.bytes[2]}});
            return;
        case midi::kProgramChange:
        case midi::kChannelPressure:
        case midi::kPitchBend:
            push(out_, {at, {status, event.bytes[1], event.bytes[2]}});
            return;
        default:
            return;
    }
}

void Arranger::noteOn(int channel, const ChannelRule& rule, std::uint8_t note, std::uint8_t velocity, Ticks at) {
    if (!playsOn(rule, chord_)) {
        return;
    }
    noteOff(channel, note, at);
    const bool megaVoice = bankMsb_.at(static_cast<std::size_t>(channel)) == kMegaVoiceBankMsb;
    Held& held = held_.at(static_cast<std::size_t>(channel)).at(note);
    held = {.on = true,
            .destination = static_cast<std::uint8_t>(rule.destination),
            .note = static_cast<std::uint8_t>(transposeNote(rule, note, chord_, megaVoice)),
            .velocity = velocity};
    push(out_, {at, {onChannel(midi::kNoteOn, held.destination), held.note, velocity}});
}

void Arranger::noteOff(int channel, std::uint8_t note, Ticks at) {
    Held& held = held_.at(static_cast<std::size_t>(channel)).at(note);
    if (held.on) {
        push(out_, {at, {onChannel(midi::kNoteOff, held.destination), held.note, 0}});
        held.on = false;
    }
}

void Arranger::releaseAll(Ticks at, std::vector<MidiEvent>& into) {
    for (auto& notes : held_) {
        for (Held& held : notes) {
            if (held.on) {
                push(into, {at, {onChannel(midi::kNoteOff, held.destination), held.note, 0}});
                held.on = false;
            }
        }
    }
}

void Arranger::push(std::vector<MidiEvent>& into, const MidiEvent& event) {
    if (into.size() < into.capacity()) {
        into.push_back(event);
    }
}

}  // namespace emerick::arranger
