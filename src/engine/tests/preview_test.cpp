#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "rig.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;

TEST_CASE("a preview plays once on its track's output while stopped and then lets the stream close") {
    Rig rig;
    Track vox{.id = "vox", .kind = TrackKind::audio};
    vox.audioOut = {"interface", {1, 2}};
    rig.engine.setSession({.tempo = {}, .tracks = {vox}});
    CHECK_FALSE(rig.audio.isOpen());

    rig.engine.playPreview("vox", {std::vector<float>(kBlock * 3, 0.25F)});
    CHECK(rig.audio.isOpen());
    CHECK(rig.engine.previewPlaying());
    rig.audio.process(kBlock * 4);
    rig.engine.releaseIdleStream();
    rig.audio.process(kBlock);
    rig.engine.releaseIdleStream();

    const std::vector<float> left = rig.audio.output("interface", 1);
    const std::vector<float> right = rig.audio.output("interface", 2);
    REQUIRE(left.size() == kBlock * 4);
    CHECK(left[kBlock * 2] == 0.25F);
    CHECK(right[kBlock * 2] == 0.25F);
    CHECK(left[kBlock * 3] == 0.0F);
    CHECK_FALSE(rig.engine.previewPlaying());
    CHECK_FALSE(rig.audio.isOpen());
}

TEST_CASE("a new preview replaces the one playing") {
    Rig rig;
    Track vox{.id = "vox", .kind = TrackKind::audio};
    vox.audioOut = {"interface", {1, 2}};
    rig.engine.setSession({.tempo = {}, .tracks = {vox}});
    rig.engine.playPreview("vox", {std::vector<float>(kBlock * 8, 0.25F)});
    rig.audio.process(kBlock);
    rig.engine.playPreview("vox", {std::vector<float>(kBlock, 0.5F)});
    rig.audio.process(kBlock * 2);
    const std::vector<float> left = rig.audio.output("interface", 1);
    CHECK(left[kBlock] == 0.5F);
    CHECK(left[kBlock * 2] == 0.0F);
}
