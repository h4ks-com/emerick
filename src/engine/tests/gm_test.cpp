#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numeric>
#include <ranges>
#include <span>
#include <string>
#include <vector>

#include "emerick/engine/clap_instrument.hpp"
#include "emerick/engine/clap_scan.hpp"

using namespace emerick::engine;

namespace {

constexpr double kRate = 48000.0;
constexpr emerick::core::Frames kBlock = 512;
using Message = std::array<std::uint8_t, 3>;

std::shared_ptr<ClapInstrument> generalMidi() {
    auto created = ClapInstrument::create(EMERICK_GM_CLAP, "org.emerick.gm");
    REQUIRE(created.has_value());
    return *created;
}

// Plays the messages at the start and returns a quarter second of the left channel.
std::vector<float> render(const std::vector<Message>& messages) {
    const std::shared_ptr<ClapInstrument> instrument = generalMidi();
    REQUIRE(instrument->activate(kRate, kBlock));
    for (const Message& message : messages) {
        instrument->queueMidi(message, 0);
    }
    std::vector<float> left;
    std::vector<float> right(kBlock);
    for (int block = 0; block < 24; ++block) {
        std::vector<float> chunk(kBlock);
        std::ranges::fill(right, 0.0F);
        const std::array<float*, 2> mix{chunk.data(), right.data()};
        instrument->processBlock(mix, kBlock, 0);
        left.insert(left.end(), chunk.begin(), chunk.end());
    }
    return left;
}

float loudest(std::span<const float> samples) {
    return std::ranges::max(samples | std::views::transform([](float sample) { return std::abs(sample); }));
}

double energy(std::span<const float> samples) {
    return std::transform_reduce(samples.begin(), samples.end(), 0.0, std::plus{},
                                 [](float sample) { return double{sample} * sample; });
}

std::size_t zeroCrossings(std::span<const float> samples) {
    return static_cast<std::size_t>(std::ranges::count_if(
        std::views::iota(std::size_t{1}, samples.size()),
        [&](std::size_t index) { return (samples[index - 1] < 0.0F) != (samples[index] < 0.0F); }));
}

}  // namespace

TEST_CASE("the General MIDI instrument loads through the CLAP host as an instrument") {
    const auto described = describeClapBundle(EMERICK_GM_CLAP);
    REQUIRE(described.has_value());
    REQUIRE(described->size() == 1);
    CHECK(described->front().id == "org.emerick.gm");
    CHECK(described->front().name == "General MIDI");
    CHECK(std::ranges::contains(described->front().features, std::string{"instrument"}));
    CHECK_FALSE(generalMidi()->hasEditor());
}

TEST_CASE("a note-on plays the bundled SoundFont") {
    CHECK(loudest(render({{0x90, 60, 100}})) > 0.01F);
    CHECK(loudest(render({})) < 1e-4F);
}

TEST_CASE("a program change plays another General MIDI sound") {
    const std::vector<float> piano = render({{0x90, 72, 100}});
    const std::vector<float> flute = render({{0xC0, 73, 0}, {0x90, 72, 100}});
    REQUIRE(loudest(flute) > 0.01F);
    const auto difference =
        std::views::zip_transform([](float left, float right) { return left - right; }, piano, flute) |
        std::ranges::to<std::vector>();
    CHECK(energy(difference) > 0.5 * energy(piano));
}

TEST_CASE("channel 10 plays the drum kit") {
    const std::vector<float> keys = render({{0x90, 42, 100}});
    const std::vector<float> hiHat = render({{0x99, 42, 100}});
    REQUIRE(loudest(hiHat) > 0.01F);
    CHECK(zeroCrossings(hiHat) > 4 * zeroCrossings(keys));
}

TEST_CASE("the instrument saves the SoundFont path it plays and refuses to play a missing one") {
    const std::shared_ptr<ClapInstrument> instrument = generalMidi();
    CHECK(instrument->saveState() == std::vector<std::uint8_t>{});
    const std::string missing = "/no/such/sounds.sf2";
    REQUIRE(instrument->loadState(std::vector<std::uint8_t>(missing.begin(), missing.end())));
    CHECK(instrument->saveState() == std::vector<std::uint8_t>(missing.begin(), missing.end()));
    CHECK_FALSE(instrument->activate(kRate, kBlock));
}
