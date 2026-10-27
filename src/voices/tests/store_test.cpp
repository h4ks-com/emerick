#include "store.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <ranges>
#include <vector>

#include "synthetic_store.hpp"
#include "wave.hpp"

using namespace emerick::voices;
using namespace emerick::voices::test;

TEST_CASE("Yamaha note names count C-2 as MIDI note 0") {
    CHECK(yamahaNote("C-2") == 0);
    CHECK(yamahaNote("C3") == 60);
    CHECK(yamahaNote("F#3") == 66);
    CHECK(yamahaNote("A3") == 69);
    CHECK(yamahaNote("G8") == 127);
    CHECK_FALSE(yamahaNote("H2").has_value());
    CHECK_FALSE(yamahaNote("C").has_value());
}

TEST_CASE("a linear wave blob decodes to its samples and sustain loop") {
    const std::vector<std::int16_t> samples = sine(440.0, 44100.0, 1000);
    const auto wave = decodeWave(linearBlob(samples, 44100.0, std::pair{100U, 400U}));
    REQUIRE(wave.has_value());
    REQUIRE(wave->samples.size() == samples.size());
    CHECK(wave->samples[37] == Catch::Approx(samples[37] / 32768.0F));
    CHECK(wave->loopStart == 100);
    CHECK(wave->loopEnd == 400);
    CHECK(wave->looped());
    const auto plain = decodeWave(linearBlob(samples, 44100.0));
    REQUIRE(plain.has_value());
    CHECK_FALSE(plain->looped());
}

TEST_CASE("a compressed WXC1 blob is not decoded") {
    CHECK_FALSE(decodeWave(compressedBlob()).has_value());
    CHECK_FALSE(decodeWave(std::vector<std::uint8_t>{1, 2, 3}).has_value());
}

TEST_CASE("the store reads each voice's pack, name, address and zones") {
    SyntheticStore store;
    const std::string wave = store.addWave(linearBlob(sine(440.0, 32000.0, 3200), 32000.0));
    store.addPack("Test Pack", {SyntheticVoice{.name = "Flute",
                                               .msb = 104,
                                               .lsb = 9,
                                               .program = 21,
                                               .keyShift = -12,
                                               .zones = {SyntheticZone{.keyLo = "C3",
                                                                       .keyHi = "B3",
                                                                       .velocityLo = 1,
                                                                       .velocityHi = 90,
                                                                       .rootKey = "F#3",
                                                                       .tuneCoarse = 1,
                                                                       .tuneFine = -21.9,
                                                                       .sampleRate = 32000.0,
                                                                       .oneShot = true,
                                                                       .wave = wave}}},
                                SyntheticVoice{.name = "Bass", .msb = 8, .zones = {SyntheticZone{.wave = wave}}}});
    const Store loaded = loadStore(store.folder());
    REQUIRE(loaded.voices.size() == 2);
    CHECK(loaded.voices[0].name == "Bass");
    const Voice& flute = loaded.voices[1];
    CHECK(flute.pack == "Test Pack");
    CHECK(flute.address == VoiceAddress{.msb = 104, .lsb = 9, .program = 21});
    REQUIRE(flute.zones.size() == 1);
    const Zone& zone = flute.zones[0];
    CHECK(zone.keyLo == 72);
    CHECK(zone.keyHi == 83);
    CHECK(zone.velocityHi == 90);
    CHECK(zone.keyShift == -12);
    CHECK(zone.rootKey == 66);
    CHECK(zone.tuneCents == Catch::Approx(78.1));
    CHECK(zone.sampleRate == 32000.0);
    CHECK(zone.oneShot);
    CHECK(zone.leftWave == wave);
    CHECK(zone.rightWave.empty());
}

TEST_CASE("the store leaves out voices whose waves are compressed or missing") {
    SyntheticStore store;
    const std::string linear = store.addWave(linearBlob(sine(440.0, 44100.0, 441), 44100.0));
    const std::string compressed = store.addWave(compressedBlob());
    store.addPack("Pack", {SyntheticVoice{.name = "Plays", .zones = {SyntheticZone{.wave = linear}}},
                           SyntheticVoice{.name = "Compressed",
                                          .zones = {SyntheticZone{.keyHi = "C3", .wave = linear},
                                                    SyntheticZone{.keyLo = "C#3", .wave = compressed}}},
                           SyntheticVoice{.name = "Missing", .zones = {SyntheticZone{.wave = std::string(40, 'f')}}}});
    const Store loaded = loadStore(store.folder());
    REQUIRE(loaded.voices.size() == 1);
    CHECK(loaded.voices[0].name == "Plays");
}

TEST_CASE("a missing or empty store folder is an empty store") {
    CHECK(loadStore("/no/such/store").voices.empty());
    const SyntheticStore store;
    CHECK(loadStore(store.folder()).voices.empty());
}
