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
constexpr std::uint32_t kFollow = 2;
constexpr std::uint32_t kHighestNote = 3;
constexpr double kFlute = 73.0;
constexpr std::uint8_t kOrgan = 16;

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

void playBlock(ClapInstrument& instrument, const Message& message) {
    instrument.queueMidi(message, 0);
    std::vector<float> left(kBlock);
    std::vector<float> right(kBlock);
    const std::array<float*, 2> mix{left.data(), right.data()};
    instrument.processBlock(mix, kBlock, 0);
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

TEST_CASE("the General MIDI instrument lists its sounds, drum kits and Follow program changes as params") {
    const std::vector<ClapParam> params = generalMidi()->params();
    REQUIRE(params.size() == 4);
    CHECK(params[0].name == "Sound");
    CHECK(params[0].enumerated);
    REQUIRE(params[0].choices.size() == 128);
    CHECK(params[0].choices[0] == "Acoustic Grand Piano");
    CHECK(params[0].choices[40] == "Violin");
    CHECK(params[0].choices[127] == "Gunshot");
    CHECK(params[1].name == "Drum kit");
    CHECK(params[1].choices == std::vector<std::string>{"Standard", "Room", "Power", "Electronic", "TR-808", "Jazz",
                                                        "Brush", "Orchestra", "SFX"});
    CHECK(params[2].name == "Follow program changes");
    CHECK(params[2].choices == std::vector<std::string>{"Off", "On"});
    CHECK(params[2].defaultValue == 1.0);
    CHECK(params[3].name == "Highest note");
    CHECK_FALSE(params[3].enumerated);
    CHECK(params[3].max == 127.0);
    CHECK(params[3].defaultValue == 127.0);
    CHECK(params[3].choices.at(95) == "95");
}

TEST_CASE("the Highest note param drops note-ons above it and keeps their note-offs") {
    CHECK(loudest(render({{0x90, 96, 100}}, {{kHighestNote, 95.0}})) < 1e-4F);
    CHECK(loudest(render({{0x90, 95, 100}}, {{kHighestNote, 95.0}})) > 0.01F);
    CHECK(loudest(render({{0x90, 96, 100}})) > 0.01F);

    const auto lastBlock = [](bool release) {
        const std::shared_ptr<ClapInstrument> instrument = generalMidi();
        REQUIRE(instrument->activate(kRate, kBlock));
        playBlock(*instrument, {0xC0, kOrgan, 0});
        playBlock(*instrument, {0x90, 96, 100});
        instrument->setParam(kHighestNote, 95.0);
        playBlock(*instrument, release ? Message{0x80, 96, 0} : Message{0xB0, 1, 0});
        std::vector<float> left(kBlock);
        std::vector<float> right(kBlock);
        const std::array<float*, 2> mix{left.data(), right.data()};
        for (int block = 0; block < 40; ++block) {
            instrument->processBlock(mix, kBlock, 0);
        }
        return left;
    };
    const std::vector<float> held = lastBlock(false);
    REQUIRE(energy(held) > 0.0);
    CHECK(energy(lastBlock(true)) < 0.1 * energy(held));
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

TEST_CASE("a program change on a melodic channel sets the Sound param and tells the host") {
    const std::shared_ptr<ClapInstrument> instrument = generalMidi();
    REQUIRE(instrument->activate(kRate, kBlock));
    playBlock(*instrument, {0xC3, 73, 0});
    CHECK(instrument->takeParamsChanged());
    CHECK(instrument->paramText(kSound, *instrument->paramValue(kSound)) == "Flute");
    playBlock(*instrument, {0xC9, 100, 0});
    playBlock(*instrument, {0xB8, 0, 127});
    playBlock(*instrument, {0xC8, 99, 0});
    CHECK_FALSE(instrument->takeParamsChanged());
    CHECK(instrument->paramValue(kSound) == kFlute);
}

TEST_CASE("a program change on a drum channel that names a kit sets the Drum kit param and tells the host") {
    const std::shared_ptr<ClapInstrument> instrument = generalMidi();
    REQUIRE(instrument->activate(kRate, kBlock));
    playBlock(*instrument, {0xC9, 25, 0});
    CHECK(instrument->takeParamsChanged());
    CHECK(instrument->paramText(kDrumKit, *instrument->paramValue(kDrumKit)) == "TR-808");
    playBlock(*instrument, {0xB8, 0, 127});
    playBlock(*instrument, {0xC8, 40, 0});
    CHECK(instrument->takeParamsChanged());
    CHECK(instrument->paramText(kDrumKit, *instrument->paramValue(kDrumKit)) == "Brush");
    playBlock(*instrument, {0xC9, 100, 0});
    CHECK_FALSE(instrument->takeParamsChanged());
    CHECK(instrument->paramText(kDrumKit, *instrument->paramValue(kDrumKit)) == "Brush");
    CHECK(instrument->paramValue(kSound) == 0.0);
}

TEST_CASE("with Follow program changes off the Sound param ignores program changes and melodic bank selects") {
    const std::vector<float> flute = render({{0x90, 72, 100}}, {{kSound, kFlute}});
    const std::vector<float> ignored =
        render({{0xB0, 0, 8}, {0xC0, 0, 0}, {0x90, 72, 100}}, {{kSound, kFlute}, {kFollow, 0.0}});
    CHECK(differenceEnergy(ignored, flute) < 0.01 * energy(flute));
}

TEST_CASE("with Follow program changes off a drum bank select still plays a drum kit") {
    const std::vector<float> keys = render({{0x98, 42, 100}});
    const std::vector<float> hiHat = render({{0xB8, 0, 127}, {0xC8, 0, 0}, {0x98, 42, 100}}, {{kFollow, 0.0}});
    REQUIRE(loudest(hiHat) > 0.01F);
    CHECK(zeroCrossings(hiHat) > 4 * zeroCrossings(keys));
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

TEST_CASE("bank select MSB 127 plays a drum kit on any channel until a melodic bank is selected") {
    const std::vector<float> keys = render({{0x98, 42, 100}});
    const std::vector<float> hiHat = render({{0xB8, 0, 127}, {0xC8, 0, 0}, {0x98, 42, 100}});
    REQUIRE(loudest(hiHat) > 0.01F);
    CHECK(zeroCrossings(hiHat) > 4 * zeroCrossings(keys));
    const std::vector<float> back = render({{0xB8, 0, 127}, {0xC8, 0, 0}, {0xB8, 0, 0}, {0xC8, 0, 0}, {0x98, 42, 100}});
    CHECK(differenceEnergy(back, keys) < 0.01 * energy(keys));
}

TEST_CASE("a drum bank select on channel 10 keeps the drums, and a kit the SoundFont lacks plays the Standard kit") {
    const std::vector<float> standard = render({{0x99, 42, 100}});
    for (const std::uint8_t channel : {std::uint8_t{8}, std::uint8_t{9}}) {
        const auto status = [&](int kind) { return static_cast<std::uint8_t>(kind | channel); };
        const std::vector<float> missingKit =
            render({{status(0xB0), 0, 127}, {status(0xB0), 32, 0}, {status(0xC0), 100, 0}, {status(0x90), 42, 100}});
        CHECK(differenceEnergy(missingKit, standard) < 0.01 * energy(standard));
    }
}

TEST_CASE(
    "the instrument saves its SoundFont path, sound, drum kit and Follow program changes, and reads a plain path as "
    "older projects saved") {
    const std::shared_ptr<ClapInstrument> instrument = generalMidi();
    instrument->setParam(kSound, kFlute);
    instrument->setParam(kDrumKit, 4.0);
    instrument->setParam(kFollow, 0.0);
    instrument->setParam(kHighestNote, 95.0);
    const auto saved = instrument->saveState();
    REQUIRE(saved.has_value());
    CHECK(std::string(saved->begin(), saved->end()) ==
          R"({"drumKit":4,"followProgramChanges":false,"highestNote":95,"sound":73,"soundFont":""})");
    const std::shared_ptr<ClapInstrument> reloaded = generalMidi();
    REQUIRE(reloaded->loadState(*saved));
    CHECK(reloaded->paramValue(kSound) == kFlute);
    CHECK(reloaded->paramText(kDrumKit, *reloaded->paramValue(kDrumKit)) == "TR-808");
    CHECK(reloaded->paramValue(kFollow) == 0.0);
    CHECK(reloaded->paramValue(kHighestNote) == 95.0);

    const std::string missing = "/no/such/sounds.sf2";
    REQUIRE(reloaded->loadState(std::vector<std::uint8_t>(missing.begin(), missing.end())));
    CHECK(reloaded->paramValue(kSound) == 0.0);
    CHECK(reloaded->paramValue(kFollow) == 1.0);
    CHECK(reloaded->paramValue(kHighestNote) == 127.0);
    const auto plain = reloaded->saveState();
    REQUIRE(plain.has_value());
    CHECK(std::string(plain->begin(), plain->end()) ==
          R"({"drumKit":0,"followProgramChanges":true,"highestNote":127,"sound":0,"soundFont":"/no/such/sounds.sf2"})");
    CHECK_FALSE(reloaded->activate(kRate, kBlock));
}
