#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <ranges>
#include <vector>

#include "emerick/app/session_mapping.hpp"
#include "emerick/model/json.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

using Bytes = std::vector<std::uint8_t>;
constexpr core::Ticks kBar = 3840;
// At 120 BPM a bar of 4/4 lasts two seconds.
constexpr core::Frames kBarFrames = 96000;

Json cycleOf(const ProjectRig& rig) { return cycleState(rig.controller->project().document()); }

Bytes sectionAt(core::Ticks position) {
    return {0xF0, 0x43, 0x7E, 0x00, static_cast<std::uint8_t>(position / kBar), 0xF7};
}

}  // namespace

TEST_CASE("toggle, set and set to clips edit the cycle as undo steps, and the transport state shows it") {
    ProjectRig rig;
    rig.controller->locate(kBar + 100);
    CHECK(rig.run("cycle.toggle").at("cycle").at("enabled") == true);
    CHECK(cycleOf(rig).at("start").at("ticks") == kBar);
    CHECK(cycleOf(rig).at("end").at("ticks") == 5 * kBar);
    CHECK(rig.registry.action("cycle.toggle")->shortcut == "C");

    rig.run("cycle.set", {{"start", "3.1.0"}, {"end", 4 * kBar}});
    CHECK(cycleOf(rig).at("start").at("bbt") == "3.1.0");
    CHECK(rig.run("transport.stop").at("cycle").at("end").at("ticks") == 4 * kBar);
    CHECK_THROWS_AS(rig.run("cycle.set", {{"start", 4 * kBar}}), AppError);
    rig.run("cycle.toggle", {{"enabled", false}});
    CHECK(cycleOf(rig).at("enabled") == false);

    const model::MidiClip first{.start = kBar, .length = kBar};
    const model::MidiClip second{.start = 6 * kBar, .length = 2 * kBar};
    const model::TrackId track{rig.run("track.addMidi").at("id").get<std::string>()};
    rig.controller->project().editTrack(track, "add clips",
                                        [&](model::Track& edited) { edited.clips = {first, second}; });
    rig.run("selection.set", {{"clips", {first.id.value, second.id.value}}});
    rig.run("cycle.setFromClips");
    CHECK(cycleOf(rig).at("enabled") == true);
    CHECK(cycleOf(rig).at("start").at("ticks") == kBar);
    CHECK(cycleOf(rig).at("end").at("ticks") == 8 * kBar);

    rig.controller->project().undo();
    CHECK(cycleOf(rig).at("enabled") == false);
    CHECK(cycleOf(rig).at("start").at("ticks") == 2 * kBar);
}

TEST_CASE("the cycle saves with the project and opens again") {
    ProjectRig rig;
    rig.run("cycle.set", {{"start", kBar}, {"end", 3 * kBar}, {"enabled", true}});
    rig.run("project.saveAs", {{"path", rig.path("Loop").string()}});
    rig.run("project.new");
    CHECK(rig.controller->project().document().cycle == model::Cycle{});

    rig.run("project.open", {{"path", rig.path("Loop.erk").string()}});
    CHECK(rig.controller->project().document().cycle == model::Cycle{.enabled = true, .start = kBar, .end = 3 * kBar});
    CHECK(rig.controller->cycle().value("enabled").toBool());
}

TEST_CASE("an audible hooked track moves the cycle start to its alignment, and each jump restarts it with its chase") {
    ProjectRig rig;
    rig.registry.addTrackType(
        {.kind = "test.style",
         .title = "Style",
         .toEngine = [](const model::Track& track, const PlaybackContext&) { return midiEngineTrack(track); },
         .chase = [](const model::Track&, core::Ticks position,
                     const PlaybackContext&) { return std::vector<MidiMessage>{{.bytes = sectionAt(position)}}; },
         .alignStart = [](const model::Track&, core::Ticks position,
                          const PlaybackContext&) -> std::optional<StartAlignment> {
             return StartAlignment{.position = position / kBar * kBar, .reason = "Style starts on the bar"};
         }});
    model::Track style;
    style.kind = "test.style";
    style.name = "Style";
    style.midiOut = {kMidiPort, 0, false, false};
    rig.controller->project().addTrack(style);
    rig.controller->project().setClockPorts({kMidiPort});
    rig.run("cycle.set", {{"start", kBar + (kBar / 2)}, {"end", 2 * kBar}, {"enabled", true}});
    rig.controller->flush();
    CHECK(rig.controller->startReason() == "Cycle: Style starts on the bar");

    rig.controller->locate(kBar);
    rig.run("transport.play");
    rig.audio->process(kBarFrames + 1000);
    CHECK(rig.controller->engine().position().ticks < kBar + (kBar / 2));
    REQUIRE(processUntil([&] { return rig.controller->engine().midiFlushed(); }));
    const auto sent = rig.midi->sent(kMidiPort) | std::views::transform(&engine::SentMidi::bytes) |
                      std::views::filter([](const Bytes& bytes) { return bytes != Bytes{0xF8}; }) |
                      std::ranges::to<std::vector>();
    const auto stop = std::ranges::find(sent, Bytes{0xFC});
    REQUIRE(std::distance(stop, sent.end()) >= 3);
    CHECK(*std::next(stop) == sectionAt(kBar));
    CHECK(*std::next(stop, 2) == Bytes{0xFA});

    rig.run("cycle.toggle");
    rig.controller->flush();
    CHECK(rig.controller->startReason() == "Style starts on the bar");
    rig.run("transport.stop");
    CHECK(rig.controller->startReason().isEmpty());
}
