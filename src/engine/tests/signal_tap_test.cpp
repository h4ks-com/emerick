#include "emerick/engine/signal_tap.hpp"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "rig.hpp"

using Catch::Approx;
using namespace emerick::engine;
using namespace emerick::engine::test;

TEST_CASE("the sample ring reads back what it took in order across its wrap") {
    SampleRing ring(8);
    std::vector<float> out(8);
    CHECK(ring.write(std::vector<float>{1, 2, 3, 4, 5, 6}) == 6);
    CHECK(ring.read(std::span(out).first(4)) == 4);
    CHECK(ring.write(std::vector<float>{7, 8, 9, 10, 11}) == 5);
    CHECK(ring.readable() == 7);
    CHECK(ring.read(out) == 7);
    CHECK(out == std::vector<float>{5, 6, 7, 8, 9, 10, 11, 0});
}

TEST_CASE("a full sample ring drops what does not fit and keeps what it held") {
    SampleRing ring(4);
    CHECK(ring.write(std::vector<float>{1, 2, 3}) == 3);
    CHECK(ring.write(std::vector<float>{4, 5, 6}) == 1);
    std::vector<float> out(8);
    CHECK(ring.read(out) == 4);
    CHECK(std::vector(out.begin(), out.begin() + 4) == std::vector<float>{1, 2, 3, 4});
}

TEST_CASE("the sample ring writes the mean of its channels") {
    SampleRing ring(4);
    const std::vector<float> left{1.0F, 0.5F};
    const std::vector<float> right{0.0F, -0.5F};
    const std::array channels{left.data(), right.data()};
    CHECK(ring.writeMean(channels, 2) == 2);
    std::vector<float> out(2);
    ring.read(out);
    CHECK(out == std::vector<float>{0.5F, 0.0F});
}

TEST_CASE("a reader thread gets every sample a writer thread sends, in order") {
    constexpr std::size_t kTotal = 200000;
    SampleRing ring(256);
    std::jthread writer([&ring] {
        std::size_t sent = 0;
        while (sent < kTotal) {
            const auto value = static_cast<float>(sent);
            sent += ring.write(std::span(&value, 1));
        }
    });
    std::size_t received = 0;
    bool ordered = true;
    std::vector<float> chunk(64);
    while (received < kTotal) {
        const std::size_t count = ring.read(chunk);
        for (std::size_t index = 0; index < count; ++index) {
            ordered = ordered && chunk[index] == static_cast<float>(received + index);
        }
        received += count;
    }
    CHECK(ordered);
}

TEST_CASE("taps carry a track's live input, its output after the fader and the master") {
    Rig rig;
    Track vox{.id = "vox", .kind = TrackKind::audio, .monitor = true, .gain = 0.5F};
    vox.audioIn = {"interface", {1}};
    vox.audioOut = {"interface", {1, 2}};
    auto input = std::make_shared<SignalTap>(std::string{"vox"}, TapPoint::input);
    auto output = std::make_shared<SignalTap>(std::string{"vox"}, TapPoint::output);
    auto master = std::make_shared<SignalTap>();
    rig.engine.setSession({.tracks = {vox}, .panLaw = PanLaw::zeroDb, .taps = {input, output, master}});
    rig.audio.setInput("interface", 1, std::vector<float>(kBlock * 4, 0.8F));
    rig.audio.process(kBlock * 4);

    const auto lastSample = [](SignalTap& tap) {
        std::vector<float> samples(tap.ring.readable());
        tap.ring.read(samples);
        REQUIRE(samples.size() >= static_cast<std::size_t>(kBlock));
        return samples.back();
    };
    CHECK(lastSample(*input) == Approx(0.8F));
    CHECK(lastSample(*output) == Approx(0.4F));
    CHECK(lastSample(*master) == Approx(0.4F));
}
