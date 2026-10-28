#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace emerick::arranger::test {

using Bytes = std::vector<std::uint8_t>;

inline void appendBigEndian(Bytes& into, std::uint32_t value, int count) {
    for (int shift = (count - 1) * 8; shift >= 0; shift -= 8) {
        into.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

inline void appendText(Bytes& into, std::string_view text) {
    std::ranges::transform(text, std::back_inserter(into),
                           [](char letter) { return static_cast<std::uint8_t>(letter); });
}

inline Bytes chunk(std::string_view id, std::span<const std::uint8_t> body) {
    Bytes result;
    appendText(result, id);
    appendBigEndian(result, static_cast<std::uint32_t>(body.size()), 4);
    result.insert(result.end(), body.begin(), body.end());
    return result;
}

inline void appendVlq(Bytes& into, std::uint32_t value) {
    Bytes groups{static_cast<std::uint8_t>(value & 0x7F)};
    while ((value >>= 7) != 0) {
        groups.insert(groups.begin(), static_cast<std::uint8_t>((value & 0x7F) | 0x80));
    }
    into.insert(into.end(), groups.begin(), groups.end());
}

// Writes an original style as SFF bytes: one SMF track with section markers, then CASM.
class StyleBuilder {
public:
    static constexpr int kTicksPerQuarter = 480;
    static constexpr int kBar = kTicksPerQuarter * 4;

    void message(std::int64_t tick, std::initializer_list<std::uint8_t> bytes) { events_.push_back({tick, bytes}); }

    void meta(std::int64_t tick, std::uint8_t type, std::string_view text) {
        Bytes bytes{0xFF, type};
        appendVlq(bytes, static_cast<std::uint32_t>(text.size()));
        appendText(bytes, text);
        events_.push_back({tick, bytes});
    }

    void marker(std::int64_t tick, std::string_view name) { meta(tick, 0x06, name); }

    void note(int channel, int pitch, int velocity, std::int64_t tick, std::int64_t length) {
        message(tick, {static_cast<std::uint8_t>(0x90 | channel), static_cast<std::uint8_t>(pitch),
                       static_cast<std::uint8_t>(velocity)});
        message(tick + length, {static_cast<std::uint8_t>(0x80 | channel), static_cast<std::uint8_t>(pitch), 0});
    }

    void voice(int channel, int msb, int lsb, int program, int volume, int pan) {
        const auto control = static_cast<std::uint8_t>(0xB0 | channel);
        message(0, {control, 0, static_cast<std::uint8_t>(msb)});
        message(0, {control, 32, static_cast<std::uint8_t>(lsb)});
        message(0, {static_cast<std::uint8_t>(0xC0 | channel), static_cast<std::uint8_t>(program)});
        message(0, {control, 7, static_cast<std::uint8_t>(volume)});
        message(0, {control, 10, static_cast<std::uint8_t>(pan)});
    }

    [[nodiscard]] Bytes smf(std::int64_t endTick) const {
        auto events = events_;
        std::ranges::stable_sort(events, {}, &Event::tick);
        Bytes track;
        std::int64_t last = 0;
        for (const auto& event : events) {
            appendVlq(track, static_cast<std::uint32_t>(event.tick - last));
            last = event.tick;
            track.insert(track.end(), event.bytes.begin(), event.bytes.end());
        }
        appendVlq(track, static_cast<std::uint32_t>(endTick - last));
        track.insert(track.end(), {0xFF, 0x2F, 0x00});
        const Bytes header{0,
                           0,
                           0,
                           1,
                           static_cast<std::uint8_t>(kTicksPerQuarter >> 8),
                           static_cast<std::uint8_t>(kTicksPerQuarter & 0xFF)};
        Bytes result = chunk("MThd", header);
        const Bytes trackChunk = chunk("MTrk", track);
        result.insert(result.end(), trackChunk.begin(), trackChunk.end());
        return result;
    }

private:
    struct Event {
        std::int64_t tick = 0;
        Bytes bytes;
    };
    std::vector<Event> events_;
};

// NTR, NTT, high key, note low limit, note high limit, RTR.
using TranspositionBytes = std::array<std::uint8_t, 6>;

struct TableSpec {
    int source = 8;
    int destination = 8;
    std::uint16_t roots = 0x0FFF;
    std::uint64_t types = 0x3FFFFFFFF;
    int sourceRoot = 0;
    int sourceType = 2;
    int middleLow = 0;
    int middleHigh = 127;
    std::array<TranspositionBytes, 3> ranges{};
};

inline Bytes tableHead(const TableSpec& spec) {
    Bytes body{static_cast<std::uint8_t>(spec.source)};
    appendText(body, "Part    ");
    body.push_back(static_cast<std::uint8_t>(spec.destination));
    body.push_back(1);
    appendBigEndian(body, spec.roots, 2);
    body.push_back(static_cast<std::uint8_t>(spec.types >> 32));
    appendBigEndian(body, static_cast<std::uint32_t>(spec.types), 4);
    body.push_back(static_cast<std::uint8_t>(spec.sourceRoot));
    body.push_back(static_cast<std::uint8_t>(spec.sourceType));
    return body;
}

inline Bytes ctab(const TableSpec& spec) {
    Bytes body = tableHead(spec);
    body.insert(body.end(), spec.ranges[1].begin(), spec.ranges[1].end());
    body.push_back(0);
    return chunk("Ctab", body);
}

inline Bytes ctb2(const TableSpec& spec) {
    Bytes body = tableHead(spec);
    body.push_back(static_cast<std::uint8_t>(spec.middleLow));
    body.push_back(static_cast<std::uint8_t>(spec.middleHigh));
    for (const auto& range : spec.ranges) {
        body.insert(body.end(), range.begin(), range.end());
    }
    body.resize(body.size() + 7, 0);
    return chunk("Ctb2", body);
}

struct Segment {
    std::string_view sections;
    std::vector<Bytes> tables;
};

inline Bytes casm(const std::vector<Segment>& segments) {
    Bytes body;
    for (const auto& segment : segments) {
        Bytes names;
        appendText(names, segment.sections);
        Bytes inner = chunk("Sdec", names);
        for (const auto& table : segment.tables) {
            inner.insert(inner.end(), table.begin(), table.end());
        }
        const Bytes cseg = chunk("CSEG", inner);
        body.insert(body.end(), cseg.begin(), cseg.end());
    }
    return chunk("CASM", body);
}

inline Bytes concat(Bytes first, const Bytes& second) {
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

// Drum pitches tell the sections apart: every section plays its own drum note on every beat.
enum DrumNote : std::uint8_t {
    kMainADrum = 36,
    kMainBDrum = 38,
    kFillADrum = 40,
    kFillBDrum = 41,
    kBreakDrum = 42,
    kIntroDrum = 44,
    kEndingDrum = 46,
};

inline constexpr int kDrums = 8;
inline constexpr int kBass = 10;
inline constexpr int kChords = 11;
inline constexpr int kPad = 13;
// The bass's reverb send, chorus send, expression and resonance in the demo style's setup block.
inline constexpr std::array<std::pair<std::uint8_t, std::uint8_t>, 4> kBassMix{
    {{91, 40}, {93, 20}, {11, 120}, {71, 70}}};

// An original style: Main A of two bars and every other section of one, a bass root of a beat on each bar, a CMaj7
// stab on each bar, and an expression change on the pad at each section start.
inline Bytes demoStyle() {
    StyleBuilder builder;
    constexpr int kBeat = StyleBuilder::kTicksPerQuarter;
    constexpr int kBar = StyleBuilder::kBar;
    builder.meta(0, 0x51, std::string_view("\x09\x27\xC0", 3));
    builder.meta(0, 0x58, std::string_view("\x04\x02\x18\x08", 4));
    builder.marker(0, "SFF2");
    builder.marker(0, "SInt");
    builder.voice(kDrums, 127, 0, 0, 110, 64);
    builder.voice(kBass, 0, 0, 33, 100, 60);
    for (const auto& [cc, value] : kBassMix) {
        builder.message(0, {0xB0 | kBass, cc, value});
    }
    builder.voice(kChords, 8, 2, 25, 90, 70);
    const std::vector<std::pair<std::string_view, std::pair<DrumNote, int>>> sections{
        {"Main A", {kMainADrum, 2}},     {"Main B", {kMainBDrum, 1}},     {"Fill In AA", {kFillADrum, 1}},
        {"Fill In BB", {kFillBDrum, 1}}, {"Fill In BA", {kBreakDrum, 1}}, {"Intro A", {kIntroDrum, 1}},
        {"Ending A", {kEndingDrum, 1}},
    };
    std::int64_t tick = kBar;
    for (const auto& [name, content] : sections) {
        builder.marker(tick, name);
        builder.message(tick, {0xB0 | kPad, 11, 90});
        for (int bar = 0; bar < content.second; ++bar) {
            const std::int64_t barStart = tick + static_cast<std::int64_t>(bar) * kBar;
            for (int beat = 0; beat < 4; ++beat) {
                builder.note(kDrums, content.first, 100, barStart + static_cast<std::int64_t>(beat) * kBeat, 10);
            }
            builder.note(kBass, 36, 100, barStart, kBeat);
            for (const int pitch : {64, 67, 71}) {
                builder.note(kChords, pitch, 80, barStart, kBeat / 2);
            }
        }
        tick += static_cast<std::int64_t>(content.second) * kBar;
    }
    builder.marker(tick, "End");
    const TableSpec drums{.source = kDrums,
                          .destination = kDrums,
                          .types = 0x7FFFFFFFF,
                          .ranges = {{{1, 0, 6, 0, 127, 0}, {1, 0, 6, 0, 127, 0}, {1, 0, 6, 0, 127, 0}}}};
    const TableSpec bass{.source = kBass,
                         .destination = kBass,
                         .ranges = {{{0, 3, 7, 28, 60, 4}, {0, 3, 7, 28, 60, 4}, {0, 3, 7, 28, 60, 4}}}};
    const TableSpec chords{.source = kChords,
                           .destination = kChords,
                           .ranges = {{{0, 2, 7, 48, 84, 1}, {0, 2, 7, 48, 84, 1}, {0, 2, 7, 48, 84, 1}}}};
    const Bytes table = casm(
        {{"Main A,Main B,Fill In AA,Fill In BB,Fill In BA,Intro A,Ending A", {ctab(drums), ctab(bass), ctab(chords)}}});
    return concat(builder.smf(tick + kBar), table);
}

}  // namespace emerick::arranger::test
