#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdint>
#include <optional>
#include <ranges>
#include <vector>

#include "emerick/app/session_mapping.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

using Bytes = std::vector<std::uint8_t>;
constexpr core::Ticks kBar = 3840;

std::vector<Bytes> sentBytes(const ProjectRig& rig) {
    REQUIRE(processUntil([&] { return rig.controller->engine().midiFlushed(); }));
    return rig.midi->sent(kMidiPort) | std::views::transform(&engine::SentMidi::bytes) | std::ranges::to<std::vector>();
}

Bytes sectionAt(core::Ticks position) {
    return {0xF0, 0x43, 0x7E, 0x00, static_cast<std::uint8_t>(position / kBar), 0xF7};
}

// A Style-like track that starts play on its bar line and chases the bar it starts on.
void addStyleType(ProjectRig& rig) {
    rig.registry.addTrackType(
        {.kind = "test.style",
         .title = "Style",
         .toEngine = [](const model::Track& track, const PlaybackContext&) { return midiEngineTrack(track); },
         .panic = [](const model::Track&) { return std::vector<MidiMessage>{{.bytes = {0xBF, 0, 127}}}; },
         .chase = [](const model::Track&, core::Ticks position,
                     const PlaybackContext&) { return std::vector<MidiMessage>{{.bytes = sectionAt(position)}}; },
         .alignStart = [](const model::Track&, core::Ticks position,
                          const PlaybackContext&) -> std::optional<StartAlignment> {
             return StartAlignment{.position = position / kBar * kBar, .reason = "Style starts on the bar"};
         }});
}

model::Track styleTrack() {
    model::Track style;
    style.kind = "test.style";
    style.name = "Style";
    style.midiOut = {kMidiPort, 0, false, false};
    return style;
}

}  // namespace

TEST_CASE("play moves to the start a playing track aligns to and sends its chase first") {
    ProjectRig rig;
    addStyleType(rig);
    rig.controller->project().addTrack(styleTrack());
    rig.controller->locate(kBar + (kBar / 2));
    rig.midi->clearSent();

    const Json state = rig.run("transport.play");
    CHECK(state.at("startReason") == "Style starts on the bar");
    CHECK(state.at("position").at("ticks").get<core::Ticks>() < kBar + (kBar / 2));
    const auto sent = sentBytes(rig);
    REQUIRE_FALSE(sent.empty());
    CHECK(sent.front() == sectionAt(kBar));
    CHECK(rig.run("transport.stop").at("startReason") == "");
}

TEST_CASE("a muted hooked track neither moves the start nor chases") {
    ProjectRig rig;
    addStyleType(rig);
    model::Track style = styleTrack();
    style.mute = true;
    rig.controller->project().addTrack(style);
    rig.controller->locate(kBar + (kBar / 2));
    rig.midi->clearSent();

    const Json state = rig.run("transport.play");
    CHECK(state.at("startReason") == "");
    CHECK(state.at("position").at("ticks").get<core::Ticks>() >= kBar + (kBar / 2));
    CHECK(sentBytes(rig).empty());
}

TEST_CASE("panic sends each track type's messages, then sustain, sound and notes off on every channel") {
    ProjectRig rig;
    addStyleType(rig);
    model::Track style = styleTrack();
    style.mute = true;
    rig.controller->project().addTrack(style);
    rig.controller->flush();

    rig.run("transport.panic");
    const auto sent = sentBytes(rig);
    REQUIRE(sent.size() == 1 + (16 * 3));
    CHECK(sent[0] == Bytes{0xBF, 0, 127});
    CHECK(sent[1] == Bytes{0xB0, 64, 0});
    CHECK(sent[2] == Bytes{0xB0, 120, 0});
    CHECK(sent[3] == Bytes{0xB0, 123, 0});
    CHECK(sent.back() == Bytes{0xBF, 123, 0});
    CHECK(rig.registry.action("transport.panic")->shortcut == "Ctrl+Shift+P");
}

TEST_CASE("a plugin's sync status shows in the transport state until it clears it") {
    ProjectRig rig;
    rig.controller->setSyncStatus("yamaha",
                                  {.label = "Main B 3/4", .detail = "No clock from the keyboard", .ok = false});
    CHECK(rig.run("transport.stop").at("sync") == Json::array({{{"source", "yamaha"},
                                                                {"label", "Main B 3/4"},
                                                                {"detail", "No clock from the keyboard"},
                                                                {"ok", false}}}));
    CHECK(rig.controller->syncBadges().size() == 1);
    rig.controller->setSyncStatus("yamaha", {});
    CHECK(rig.run("transport.stop").at("sync").empty());
}

TEST_CASE("the chase setting turns the core MIDI chase off") {
    ProjectRig rig;
    const bool chase = GENERATE(true, false);
    rig.registry.setSetting("midi.chase", chase);
    model::Track keys;
    keys.kind = "midi";
    keys.name = "Keys";
    keys.midiOut = {kMidiPort, 1, false, false};
    keys.clips.emplace_back(model::MidiClip{.length = 2 * kBar, .events = {{0, {0xC0, 7}}}});
    rig.controller->project().addTrack(keys);
    rig.controller->locate(kBar);
    rig.midi->clearSent();

    rig.run("transport.play");
    const auto sent = sentBytes(rig);
    CHECK(std::ranges::contains(sent, Bytes{0xC0, 7}) == chase);
}
