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

constexpr std::uint32_t kSound = 0;
constexpr std::uint32_t kDrumKit = 1;
constexpr double kFlute = 73.0;

struct ParamValue {
    std::uint32_t id = 0;
    double value = 0.0;
};

// Sets the params, plays the messages at the start and returns a quarter second of the left channel.
std::vector<float> render(const std::vector<Message>& messages, const std::vector<ParamValue>& params = {}) {
    const std::shared_ptr<ClapInstrument> instrument = generalMidi();
    REQUIRE(instrument->activate(kRate, kBlock));
    for (const ParamValue& param : params) {
        instrument->setParam(param.id, param.value);
    }
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

double differenceEnergy(std::span<const float> left, std::span<const float> right) {
    return energy(std::views::zip_transform([](float first, float second) { return first - second; }, left, right) |
                  std::ranges::to<std::vector>());
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
    CHECK(differenceEnergy(piano, flute) > 0.5 * energy(piano));
}

TEST_CASE("the General MIDI instrument lists its sounds and drum kits as params") {
    const std::vector<ClapParam> params = generalMidi()->params();
    REQUIRE(params.size() == 2);
    CHECK(params[0].name == "Sound");
    CHECK(params[0].enumerated);
    REQUIRE(params[0].choices.size() == 128);
    CHECK(params[0].choices[0] == "Acoustic Grand Piano");
    CHECK(params[0].choices[40] == "Violin");
    CHECK(params[0].choices[127] == "Gunshot");
    CHECK(params[1].name == "Drum kit");
    CHECK(params[1].choices == std::vector<std::string>{"Standard", "Room", "Power", "Electronic", "TR-808", "Jazz",
                                                        "Brush", "Orchestra", "SFX"});
}

TEST_CASE("the Sound param plays its sound on every melodic channel until a program change picks another") {
    const std::vector<float> piano = render({{0x90, 72, 100}});
    const std::vector<float> flute = render({{0xC0, 73, 0}, {0x90, 72, 100}});
    const std::vector<float> picked = render({{0x90, 72, 100}}, {{kSound, kFlute}});
    CHECK(differenceEnergy(picked, flute) < 0.01 * energy(flute));
    CHECK(differenceEnergy(picked, piano) > 0.5 * energy(piano));
    const std::vector<float> otherChannel = render({{0x93, 72, 100}}, {{kSound, kFlute}});
    CHECK(differenceEnergy(otherChannel, flute) < 0.01 * energy(flute));
    const std::vector<float> overridden = render({{0xC0, 0, 0}, {0x90, 72, 100}}, {{kSound, kFlute}});
    CHECK(differenceEnergy(overridden, piano) < 0.01 * energy(piano));
}

TEST_CASE("the Drum kit param picks the kit channel 10 plays") {
    const std::vector<float> standard = render({{0x99, 38, 100}});
    const std::vector<float> electronic = render({{0x99, 38, 100}}, {{kDrumKit, 3.0}});
    REQUIRE(loudest(electronic) > 0.01F);
    CHECK(differenceEnergy(standard, electronic) > 0.5 * energy(standard));
}

TEST_CASE("channel 10 plays the drum kit") {
    const std::vector<float> keys = render({{0x90, 42, 100}});
    const std::vector<float> hiHat = render({{0x99, 42, 100}});
    REQUIRE(loudest(hiHat) > 0.01F);
    CHECK(zeroCrossings(hiHat) > 4 * zeroCrossings(keys));
}

TEST_CASE(
    "the instrument saves its SoundFont path, sound and drum kit, and reads a plain path as older projects saved") {
    const std::shared_ptr<ClapInstrument> instrument = generalMidi();
    instrument->setParam(kSound, kFlute);
    instrument->setParam(kDrumKit, 4.0);
    const auto saved = instrument->saveState();
    REQUIRE(saved.has_value());
    CHECK(std::string(saved->begin(), saved->end()) == R"({"drumKit":4,"sound":73,"soundFont":""})");
    const std::shared_ptr<ClapInstrument> reloaded = generalMidi();
    REQUIRE(reloaded->loadState(*saved));
    CHECK(reloaded->paramValue(kSound) == kFlute);
    CHECK(reloaded->paramText(kDrumKit, *reloaded->paramValue(kDrumKit)) == "TR-808");

    const std::string missing = "/no/such/sounds.sf2";
    REQUIRE(reloaded->loadState(std::vector<std::uint8_t>(missing.begin(), missing.end())));
    CHECK(reloaded->paramValue(kSound) == 0.0);
    const auto plain = reloaded->saveState();
    REQUIRE(plain.has_value());
    CHECK(std::string(plain->begin(), plain->end()) == R"({"drumKit":0,"sound":0,"soundFont":"/no/such/sounds.sf2"})");
    CHECK_FALSE(reloaded->activate(kRate, kBlock));
}
