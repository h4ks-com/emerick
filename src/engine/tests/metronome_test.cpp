#include <sndfile.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <map>
#include <ranges>

#include "emerick/engine/engine.hpp"
#include "emerick/engine/sound_file.hpp"
#include "rig.hpp"

using namespace emerick;
using namespace emerick::engine;
using namespace emerick::engine::test;

namespace {

constexpr float kAccentLevel = 1.0F;
constexpr float kBeatLevel = 0.25F;

void writeImpulse(const std::filesystem::path& path, float level) {
    const std::vector<float> samples{level, 0.0F, 0.0F, 0.0F};
    SF_INFO info{.samplerate = static_cast<int>(kRate), .channels = 1, .format = SF_FORMAT_WAV | SF_FORMAT_FLOAT};
    SNDFILE* file = openSoundFile(path, SFM_WRITE, &info);
    REQUIRE(file != nullptr);
    sf_writef_float(file, samples.data(), static_cast<sf_count_t>(samples.size()));
    sf_close(file);
}

// A metronome whose accent and beat are one sample impulses, so every click shows as one sample at its exact frame.
Metronome impulseMetronome(const TempFolder& folder) {
    writeImpulse(folder.path / "accent.wav", kAccentLevel);
    writeImpulse(folder.path / "beat.wav", kBeatLevel);
    return {.enabled = true,
            .duringPlayback = true,
            .mode = MetronomeMode::sample,
            .gain = 1.0F,
            .accentGain = 1.0F,
            .audioOut = {"interface", {1}},
            .accentSample = folder.path / "accent.wav",
            .beatSample = folder.path / "beat.wav"};
}

// Plays from the start and returns the output frame where the timeline's frame 0 sits.
Frames playFromStart(Rig& rig) {
    rig.engine.locate(0);
    rig.audio.process(rig.engine.layout().maxFrames);
    const Frames start = rig.audio.processedFrames();
    rig.engine.play();
    return start;
}

std::map<Frames, float> nonSilent(const std::vector<float>& output, Frames from) {
    std::map<Frames, float> found;
    for (auto index = static_cast<std::size_t>(from); index < output.size(); ++index) {
        if (output[index] != 0.0F) {
            found[static_cast<Frames>(index) - from] = output[index];
        }
    }
    return found;
}

std::vector<SentMidi> noteOns(const Rig& rig, const std::string& port) {
    return rig.midi.sent(port) | std::views::filter([](const SentMidi& message) {
               return message.bytes.size() == 3 && (message.bytes[0] & 0xF0) == 0x90 && message.bytes[2] > 0;
           }) |
           std::ranges::to<std::vector>();
}

}  // namespace

TEST_CASE("clicks land exactly on beat frames at any tempo and block size, with the accent on beat 1") {
    const double bpm = GENERATE(120.0, 97.0, 133.3);
    const Frames block = GENERATE(Frames{64}, Frames{100}, Frames{512});
    Rig rig(block);
    const TempFolder folder;
    const core::TempoMap tempo{bpm, {3, 4}};
    rig.engine.setSession({.tempo = tempo, .metronome = impulseMetronome(folder)});

    const Frames start = playFromStart(rig);
    const Frames length = tempo.ticksToFrames(tempo.ticksPerBar() * 2, kRate) + 10;
    rig.audio.process(length);

    std::map<Frames, float> expected;
    for (Ticks beat = 0; tempo.ticksToFrames(beat * tempo.ticksPerBeat(), kRate) < length; ++beat) {
        expected[tempo.ticksToFrames(beat * tempo.ticksPerBeat(), kRate)] = beat % 3 == 0 ? kAccentLevel : kBeatLevel;
    }
    std::map<Frames, float> heard = nonSilent(rig.audio.output("interface", 1), start);
    std::erase_if(heard, [&](const auto& entry) { return entry.first >= length; });
    CHECK(heard == expected);
}

TEST_CASE("the accent level and accented beats raise those clicks over the others") {
    Rig rig;
    const TempFolder folder;
    Metronome metronome = impulseMetronome(folder);
    metronome.accentGain = 2.0F;
    metronome.accentBeats = {1, 3};
    writeImpulse(folder.path / "accent.wav", kBeatLevel);
    const core::TempoMap tempo{120.0, {4, 4}};
    rig.engine.setSession({.tempo = tempo, .metronome = metronome});

    const Frames start = playFromStart(rig);
    rig.audio.process(4 * 24000);

    const std::map<Frames, float> heard = nonSilent(rig.audio.output("interface", 1), start);
    CHECK(heard == std::map<Frames, float>{{0, 0.5F}, {24000, kBeatLevel}, {48000, 0.5F}, {72000, kBeatLevel}});
}

TEST_CASE("the built-in click sounds briefly after each beat and stays silent between beats") {
    Rig rig;
    const core::TempoMap tempo{120.0, {4, 4}};
    rig.engine.setSession(
        {.tempo = tempo, .metronome = {.enabled = true, .duringPlayback = true, .audioOut = {"interface", {1, 2}}}});

    const Frames start = playFromStart(rig);
    rig.audio.process(2 * 24000);

    const std::vector<float> output = rig.audio.output("interface", 2);
    const auto peak = [&](Frames from, Frames to) {
        float loudest = 0.0F;
        for (Frames frame = from; frame < to; ++frame) {
            loudest = std::max(loudest, std::abs(output[static_cast<std::size_t>(start + frame)]));
        }
        return loudest;
    };
    CHECK(peak(0, 2400) > 0.2F);
    CHECK(peak(24000, 26400) > 0.1F);
    CHECK(peak(24000, 26400) < peak(0, 2400));
    CHECK(peak(4800, 24000) == 0.0F);
}

TEST_CASE("the metronome follows its playback and recording toggles") {
    Rig rig;
    const TempFolder folder;
    Metronome metronome = impulseMetronome(folder);
    metronome.duringPlayback = false;
    rig.engine.setSession({.tempo = {120.0, {4, 4}}, .metronome = metronome});

    const Frames start = playFromStart(rig);
    rig.audio.process(48000);
    CHECK(nonSilent(rig.audio.output("interface", 1), start).empty());

    rig.engine.stop();
    rig.engine.setRecording(true);
    const Frames recordStart = playFromStart(rig);
    rig.audio.process(100);
    CHECK(nonSilent(rig.audio.output("interface", 1), recordStart).size() == 1);
}

TEST_CASE("a count-in clicks its bars, then the take starts at the record position") {
    Rig rig;
    const TempFolder folder;
    const core::TempoMap tempo{110.0, {4, 4}};
    Metronome metronome = impulseMetronome(folder);
    metronome.enabled = false;
    metronome.countInBars = 2;
    Track track{.id = "vox", .kind = TrackKind::audio, .armed = true};
    track.audioIn = {"interface", {2}};
    rig.engine.setSession({.tempo = tempo, .tracks = {track}, .takeFolder = folder.path, .metronome = metronome});
    const Ticks recordAt = 2 * tempo.ticksPerBar();
    rig.engine.locate(recordAt);
    rig.audio.process(kBlock);

    const Frames countIn = tempo.ticksToFrames(2 * tempo.ticksPerBar(), kRate);
    REQUIRE(countIn % kBlock != 0);
    std::vector<float> ramp(static_cast<std::size_t>(countIn + 4800));
    for (std::size_t frame = 0; frame < ramp.size(); ++frame) {
        ramp[frame] = static_cast<float>(frame) * 1e-6F;
    }
    const Frames start = rig.audio.processedFrames();
    rig.audio.setInput("interface", 2, ramp);
    rig.engine.setRecording(true);
    rig.engine.play();
    rig.audio.process(countIn - 10);
    CHECK(rig.engine.playing());
    CHECK(rig.engine.position().ticks == recordAt);
    rig.audio.process(4810);
    CHECK(rig.engine.position().frames == tempo.ticksToFrames(recordAt, kRate) + 4800);
    rig.engine.stop();
    rig.audio.process(kBlock);

    const std::map<Frames, float> clicks = nonSilent(rig.audio.output("interface", 1), start);
    std::map<Frames, float> expected;
    for (Ticks beat = 0; beat < 8; ++beat) {
        expected[tempo.ticksToFrames(beat * tempo.ticksPerBeat(), kRate)] = beat % 4 == 0 ? kAccentLevel : kBeatLevel;
    }
    CHECK(clicks == expected);

    std::vector<RecordedTake> takes;
    REQUIRE(waitFor([&] {
        takes = rig.engine.takeRecordings();
        return !takes.empty();
    }));
    REQUIRE(takes.size() == 1);
    CHECK(takes[0].startTick == recordAt);
    SF_INFO info{};
    SNDFILE* file = openSoundFile(takes[0].audioFile, SFM_READ, &info);
    REQUIRE(file != nullptr);
    std::vector<float> recorded(static_cast<std::size_t>(info.frames));
    sf_readf_float(file, recorded.data(), info.frames);
    sf_close(file);
    REQUIRE(recorded.size() == 4800);
    CHECK(recorded.front() == ramp[static_cast<std::size_t>(countIn)]);
    CHECK(recorded.back() == ramp.back());
}

TEST_CASE("a metronome preview clicks two bars while stopped, even with the metronome off") {
    Rig rig;
    const TempFolder folder;
    const core::TempoMap tempo{120.0, {4, 4}};
    Metronome metronome = impulseMetronome(folder);
    metronome.enabled = false;
    rig.engine.setSession({.tempo = tempo, .metronome = metronome});
    rig.audio.process(rig.engine.layout().maxFrames);
    const Frames start = rig.audio.processedFrames();
    rig.engine.previewMetronome();
    CHECK(rig.engine.previewPlaying());
    rig.audio.process(tempo.ticksToFrames(tempo.ticksPerBar() * 3, kRate));

    std::map<Frames, float> expected;
    for (Ticks beat = 0; beat < 8; ++beat) {
        expected[tempo.ticksToFrames(beat * tempo.ticksPerBeat(), kRate)] = beat % 4 == 0 ? kAccentLevel : kBeatLevel;
    }
    CHECK(nonSilent(rig.audio.output("interface", 1), start) == expected);
    CHECK_FALSE(rig.engine.playing());
    CHECK_FALSE(rig.engine.previewPlaying());
    CHECK(rig.engine.position().frames == 0);
}

TEST_CASE("stopping during a count-in cancels it without a take") {
    Rig rig;
    const TempFolder folder;
    Track track{.id = "vox", .kind = TrackKind::audio, .armed = true};
    track.audioIn = {"interface", {2}};
    rig.engine.setSession(
        {.tempo = {120.0, {4, 4}}, .tracks = {track}, .takeFolder = folder.path, .metronome = {.countInBars = 1}});
    rig.engine.setRecording(true);
    rig.engine.play();
    rig.audio.process(24000);
    CHECK(rig.engine.playing());
    rig.engine.stop();
    rig.audio.process(96000);
    CHECK_FALSE(rig.engine.playing());
    CHECK(rig.engine.position().frames == 0);
    CHECK(rig.engine.takeRecordings().empty());
}

TEST_CASE("MIDI click mode sends the accent and beat notes on the drum channel at each beat") {
    Rig rig;
    const core::TempoMap tempo{100.0, {4, 4}};
    rig.engine.setSession(
        {.tempo = tempo,
         .metronome = {
             .enabled = true, .duringPlayback = true, .mode = MetronomeMode::midiClick, .midiOut = {kPort1, 10}}});

    const Frames start = playFromStart(rig);
    rig.audio.process(tempo.ticksToFrames(tempo.ticksPerBar(), kRate) + 10);
    REQUIRE(waitFor([&] { return rig.engine.midiFlushed(); }));

    const std::vector<SentMidi> notes = noteOns(rig, kPort1);
    REQUIRE(notes.size() == 5);
    for (std::size_t beat = 0; beat < notes.size(); ++beat) {
        const std::uint8_t note = beat % 4 == 0 ? 76 : 77;
        CHECK(notes[beat].bytes[0] == 0x99);
        CHECK(notes[beat].bytes[1] == note);
        CHECK(frameOf(notes[beat]) - start ==
              tempo.ticksToFrames(static_cast<Ticks>(beat) * tempo.ticksPerBeat(), kRate));
    }
    CHECK(notes[0].bytes[2] > notes[1].bytes[2]);
    CHECK(rig.midi.sent(kPort1).size() == 10);
}

TEST_CASE("each groove plays its kick, snare and hat hits in every bar") {
    struct Expected {
        GroovePattern pattern;
        std::size_t kicks;
        std::size_t snares;
        std::size_t hats;
    };
    const Expected expected =
        GENERATE(Expected{GroovePattern::rock, 3, 2, 8}, Expected{GroovePattern::fourOnTheFloor, 4, 2, 4},
                 Expected{GroovePattern::halfTime, 2, 1, 8}, Expected{GroovePattern::shuffle, 2, 2, 8},
                 Expected{GroovePattern::bossa, 8, 5, 8});
    Rig rig;
    const core::TempoMap tempo{120.0, {4, 4}};
    rig.engine.setSession({.tempo = tempo,
                           .metronome = {.enabled = true,
                                         .duringPlayback = true,
                                         .mode = MetronomeMode::midiGroove,
                                         .pattern = expected.pattern,
                                         .midiOut = {kPort2, 0}}});

    playFromStart(rig);
    rig.audio.process(2 * 96000);
    REQUIRE(waitFor([&] { return rig.engine.midiFlushed(); }));

    const std::vector<SentMidi> notes = noteOns(rig, kPort2);
    const auto count = [&](std::uint8_t note) {
        return static_cast<std::size_t>(
            std::ranges::count(notes, note, [](const SentMidi& sent) { return sent.bytes[1]; }));
    };
    CHECK(count(36) == 2 * expected.kicks);
    CHECK(count(38) == 2 * expected.snares);
    CHECK(count(42) == 2 * expected.hats);
    CHECK(std::ranges::all_of(notes, [](const SentMidi& sent) { return sent.bytes[0] == 0x99; }));
}

TEST_CASE("a recorded take holds only the input, never the metronome") {
    Rig rig;
    const TempFolder folder;
    Track track{.id = "vox", .kind = TrackKind::audio, .armed = true};
    track.audioIn = {"interface", {2}};
    rig.engine.setSession({.tempo = {120.0, {4, 4}},
                           .tracks = {track},
                           .takeFolder = folder.path,
                           .metronome = {.enabled = true, .duringRecording = true, .mode = MetronomeMode::groove}});
    rig.engine.setRecording(true);
    rig.engine.play();
    rig.audio.process(48000);
    rig.engine.stop();
    rig.audio.process(kBlock);

    CHECK(std::ranges::any_of(rig.audio.output("interface", 1), [](float sample) { return sample != 0.0F; }));
    std::vector<RecordedTake> takes;
    REQUIRE(waitFor([&] {
        takes = rig.engine.takeRecordings();
        return !takes.empty();
    }));
    SF_INFO info{};
    SNDFILE* file = openSoundFile(takes[0].audioFile, SFM_READ, &info);
    REQUIRE(file != nullptr);
    std::vector<float> recorded(static_cast<std::size_t>(info.frames));
    sf_readf_float(file, recorded.data(), info.frames);
    sf_close(file);
    CHECK(recorded.size() == 48000);
    CHECK(std::ranges::all_of(recorded, [](float sample) { return sample == 0.0F; }));
}

TEST_CASE("a metronome sample that cannot load is reported and the click plays instead") {
    Rig rig;
    rig.engine.setSession({.tempo = {120.0, {4, 4}},
                           .metronome = {.mode = MetronomeMode::sample, .accentSample = "/no/such/accent.wav"}});

    REQUIRE(rig.engine.sessionErrors().size() == 1);
    CHECK(rig.engine.sessionErrors()[0].trackId == kMetronomeId);
}
