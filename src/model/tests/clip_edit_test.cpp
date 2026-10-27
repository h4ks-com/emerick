#include "emerick/model/clip_edit.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <ranges>
#include <utility>
#include <variant>
#include <vector>

#include "emerick/model/midi_notes.hpp"

using namespace emerick;
using namespace emerick::model;

namespace {

constexpr double kRate = 48000.0;
const core::TempoMap kTempo{};

using Event = std::pair<Ticks, std::vector<std::uint8_t>>;

Event on(Ticks at, std::uint8_t key) { return {at, {0x90, key, 100}}; }
Event off(Ticks at, std::uint8_t key) { return {at, {0x80, key, 0}}; }

MidiClip heldNotes() {
    MidiClip clip;
    clip.start = 960;
    clip.length = 1920;
    for (auto [at, bytes] : {on(0, 60), on(100, 64), on(600, 67), off(1000, 60), off(1800, 64)}) {
        clip.events.push_back({at, std::move(bytes)});
    }
    return clip;
}

std::vector<Event> events(const Clip& clip) {
    std::vector<Event> result;
    for (const MidiEvent& event : clipEvents(std::get<MidiClip>(clip))) {
        result.emplace_back(event.at, event.bytes);
    }
    return result;
}

}  // namespace

TEST_CASE("clip start, length and end read every clip kind") {
    AudioClip audio;
    audio.start = 960;
    audio.length = 480;
    CHECK(clipStart(audio) == 960);
    CHECK(clipLength(audio) == 480);
    CHECK(clipEnd(heldNotes()) == 2880);
    CHECK(clipStart(moveClip(audio, 10)) == 10);
    const Clip copy = duplicateClip(audio, 1440);
    CHECK(clipId(copy) != audio.id);
    CHECK(clipStart(copy) == 1440);
}

TEST_CASE("trimming an audio clip's start moves its source offset and keeps its end") {
    AudioClip audio;
    audio.start = 960;
    audio.length = 1920;
    const auto trimmed = trimStart(audio, 1440, kTempo, kRate);
    REQUIRE(trimmed);
    const auto& result = std::get<AudioClip>(*trimmed);
    CHECK(result.start == 1440);
    CHECK(result.length == 1440);
    CHECK(result.sourceOffset == 12000);
    CHECK_FALSE(trimStart(audio, 480, kTempo, kRate));
    CHECK_FALSE(trimStart(audio, 2880, kTempo, kRate));
    CHECK_FALSE(setLength(audio, 0));
}

TEST_CASE("cutting through a held note ends it at the cut and drops its orphan note-off") {
    const auto trimmed = trimStart(heldNotes(), 960 + 500, kTempo, kRate);
    REQUIRE(trimmed);
    CHECK(clipLength(*trimmed) == 1420);
    CHECK(events(*trimmed) == std::vector{on(100, 67), off(1420, 67)});

    const auto shortened = setLength(heldNotes(), 500);
    REQUIRE(shortened);
    CHECK(events(*shortened) == std::vector{on(0, 60), on(100, 64), off(500, 60), off(500, 64)});
    CHECK(events(*setLength(heldNotes(), 3000)).back() == off(1920, 67));
}

TEST_CASE("splitting a MIDI clip leaves no hanging note on either side") {
    const auto parts = split(heldNotes(), 960 + 1200, kTempo, kRate);
    REQUIRE(parts);
    const auto& [left, right] = *parts;
    CHECK(clipLength(left) == 1200);
    CHECK(events(left) ==
          std::vector{on(0, 60), on(100, 64), on(600, 67), off(1000, 60), off(1200, 64), off(1200, 67)});
    CHECK(clipStart(right) == 2160);
    CHECK(clipLength(right) == 720);
    CHECK(events(right).empty());
    CHECK(clipId(right) != clipId(left));
    CHECK_FALSE(split(heldNotes(), 960, kTempo, kRate));
}

namespace {

// One bar of content with notes on beats 1 and 4, looped for two and a half bars.
MidiClip loopedBar() {
    MidiClip clip;
    clip.start = 960;
    clip.length = 3840;
    clip.loopLength = 9600;
    clip.events = std::vector{on(0, 60), off(480, 60), on(2880, 64), off(3840, 64)} |
                  std::views::transform([](const Event& event) { return MidiEvent{event.first, event.second}; }) |
                  std::ranges::to<std::vector>();
    gatherNotes(clip);
    return clip;
}

}  // namespace

TEST_CASE("a looped MIDI clip plays its content again in each repeat and cuts the last repeat short") {
    const MidiClip clip = loopedBar();
    CHECK(clipEnd(clip) == 960 + 9600);
    CHECK(events(clip) == std::vector{on(0, 60), off(480, 60), on(2880, 64), off(3840, 64), on(3840, 60), off(4320, 60),
                                      on(6720, 64), off(7680, 64), on(7680, 60), off(8160, 60)});
    MidiClip held = clip;
    held.loopLength = 7680 + 200;
    CHECK(events(held).back() == off(7880, 60));
}

TEST_CASE("setting a loop length loops only MIDI clips and a length up to the content ends the loop") {
    const auto longer = setLoopLength(loopedBar(), 20000);
    REQUIRE(longer);
    CHECK(clipEnd(*longer) == 960 + 20000);
    CHECK(clipLength(*longer) == 3840);
    CHECK(std::get<MidiClip>(*setLoopLength(loopedBar(), 3840)).loopLength == 0);
    CHECK_FALSE(setLoopLength(loopedBar(), 0));
    CHECK_FALSE(setLoopLength(AudioClip{.length = 960}, 1920));
}

TEST_CASE("resizing a looped clip's content keeps where its repeats end") {
    const auto shorter = setLength(loopedBar(), 1920);
    REQUIRE(shorter);
    CHECK(clipEnd(*shorter) == 960 + 9600);
    CHECK(events(*shorter).at(2) == on(1920, 60));
    const auto trimmed = trimStart(loopedBar(), 960 + 2880, kTempo, kRate);
    REQUIRE(trimmed);
    CHECK(clipEnd(*trimmed) == 960 + 9600);
    CHECK(events(*trimmed).front() == on(0, 64));
}

TEST_CASE("splitting a looped clip loops each side up to the split, or writes out the repeats it cuts into") {
    const auto onBoundary = split(loopedBar(), 960 + 3840, kTempo, kRate);
    REQUIRE(onBoundary);
    CHECK(clipEnd(onBoundary->first) == 960 + 3840);
    CHECK_FALSE(looped(std::get<MidiClip>(onBoundary->first)));
    CHECK(clipStart(onBoundary->second) == 960 + 3840);
    CHECK(clipEnd(onBoundary->second) == 960 + 9600);
    CHECK(std::get<MidiClip>(onBoundary->second).loopLength == 5760);

    const auto inside = split(loopedBar(), 960 + 5760, kTempo, kRate);
    REQUIRE(inside);
    CHECK(std::get<MidiClip>(inside->first).loopLength == 5760);
    CHECK(events(inside->first).back() == off(4320, 60));
    const auto& right = std::get<MidiClip>(inside->second);
    CHECK_FALSE(looped(right));
    CHECK(right.start == 960 + 5760);
    CHECK(right.length == 3840);
    CHECK(events(right) == std::vector{on(960, 64), off(1920, 64), on(1920, 60), off(2400, 60)});
}

TEST_CASE("splitting an audio clip continues the source and drops the fades at the cut") {
    AudioClip audio;
    audio.length = 1920;
    audio.fadeIn = 100;
    audio.fadeOut = 100;
    const auto parts = split(audio, 960, kTempo, kRate);
    REQUIRE(parts);
    const auto& left = std::get<AudioClip>(parts->first);
    const auto& right = std::get<AudioClip>(parts->second);
    CHECK(left.fadeIn == 100);
    CHECK(left.fadeOut == 0);
    CHECK(right.fadeIn == 0);
    CHECK(right.fadeOut == 100);
    CHECK(right.sourceOffset == 24000);
}

TEST_CASE("a crossfade fades both clips over their overlap with equal power, from either clip") {
    Track track;
    const AudioClip first{.start = 0, .length = 1920, .fadeIn = 1800};
    const AudioClip second{.start = 1440, .length = 1920};
    track.clips = {first, second, MidiClip{.start = 0, .length = 4000}};
    for (const ClipId& id : {first.id, second.id}) {
        Track edited = track;
        REQUIRE(crossfade(edited, id));
        const auto& out = std::get<AudioClip>(edited.clips[0]);
        const auto& in = std::get<AudioClip>(edited.clips[1]);
        CHECK(out.fadeOut == 480);
        CHECK(out.fadeOutShape == core::FadeShape::equalPower);
        CHECK(out.fadeIn == 1440);
        CHECK(in.fadeIn == 480);
        CHECK(in.fadeInShape == core::FadeShape::equalPower);
    }

    Track apart;
    apart.clips = {first, AudioClip{.start = 1920, .length = 960}};
    CHECK_FALSE(crossfade(apart, first.id));
    Track inside;
    inside.clips = {first, AudioClip{.start = 480, .length = 480}};
    CHECK_FALSE(crossfade(inside, first.id));
}
