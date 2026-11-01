#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/engine/effects.hpp"
#include "emerick/engine/plugin.hpp"
#include "rig.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;

namespace {

constexpr Frames kImpulseAt = 1000;
constexpr Frames kLate = 480;

std::filesystem::path impulse(const TempFolder& folder) {
    std::vector<float> samples(4800);
    samples[kImpulseAt] = 1.0F;
    const std::filesystem::path path = folder.path / "impulse.wav";
    writeWav(path, samples);
    return path;
}

Track trackTo(const std::string& id, const std::string& device, const std::filesystem::path& clip) {
    Track track{.id = id, .kind = TrackKind::audio};
    track.audioOut = {device, {1, 2}};
    track.audioClips = {{.path = clip, .start = 0, .length = 1920, .sourceOffset = 0, .gain = 1.0F}};
    return track;
}

std::shared_ptr<Plugin> late() {
    auto created = createClapPlugin(std::filesystem::path{EMERICK_TEST_CLAP_EFFECT_FOLDER} / "Emerick Test late.clap",
                                    "org.emerick.test.late");
    REQUIRE(created.has_value());
    return *created;
}

Frames loudestFrame(const std::vector<float>& samples, Frames from) {
    const auto played = std::span(samples).subspan(static_cast<std::size_t>(from));
    return static_cast<Frames>(std::ranges::max_element(played, {}, [](float sample) { return std::abs(sample); }) -
                               played.begin());
}

}  // namespace

TEST_CASE("a track without plugin latency plays later by the latency of the slowest track, so both line up") {
    TempFolder folder;
    const std::filesystem::path clip = impulse(folder);
    Track slow = trackTo("slow", "interface", clip);
    slow.inserts = {{.id = "late", .plugin = late()}};
    const Track fast = trackTo("fast", "keyboard", clip);
    Rig rig;
    rig.engine.setSession({.tracks = {slow, fast}});
    CHECK(rig.engine.latencyCompensation() == kLate);
    playOnceReadAhead(rig);
    const Frames start = rig.audio.processedFrames();
    rig.audio.process(4800);
    const Frames slowPeak = loudestFrame(rig.audio.output("interface", 1), start);
    const Frames fastPeak = loudestFrame(rig.audio.output("keyboard", 1), start);
    CHECK(slowPeak == fastPeak);
    CHECK(slowPeak == kImpulseAt + kLate);
}

TEST_CASE("a limiter's lookahead delays the other tracks too, and a bypassed plugin adds no delay") {
    TempFolder folder;
    const std::filesystem::path clip = impulse(folder);
    Track limited = trackTo("limited", "interface", clip);
    limited.inserts = {{.id = "limiter",
                        .type = EffectType::limiter,
                        .bypass = false,
                        .params = defaultParams(effectSpec(EffectType::limiter))}};
    Track bypassed = trackTo("bypassed", "keyboard", clip);
    bypassed.inserts = {{.id = "late", .bypass = true, .plugin = late()}};
    Rig rig;
    rig.engine.setSession({.tracks = {limited, bypassed}});
    CHECK(rig.engine.latencyCompensation() > 0);
    CHECK(rig.engine.latencyCompensation() < kLate);
    playOnceReadAhead(rig);
    const Frames start = rig.audio.processedFrames();
    rig.audio.process(4800);
    CHECK(loudestFrame(rig.audio.output("interface", 1), start) ==
          loudestFrame(rig.audio.output("keyboard", 1), start));
}
