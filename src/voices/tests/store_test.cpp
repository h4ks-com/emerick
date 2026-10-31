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

namespace {

std::vector<WxcFrame> crackedFrames() {
    WxcFrame first{.next = {.coefficients = {-100, 30, 5}, .scale = 30, .mode = 3}};
    WxcFrame second{.next = {.scale = 10, .mode = 5}};
    for (std::uint16_t word = 0; word < 16; ++word) {
        first.codes.at(word) =
            static_cast<std::uint16_t>(((word * 0x9E5U + 0x123U) & 0xFFFU) | (word % 3 == 0 ? 0x8000U : 0U));
        second.codes.at(word) =
            static_cast<std::uint16_t>(((word * 0x3B7U + 0x55U) & 0xFFFU) | (word % 2 == 1 ? 0x8000U : 0U));
    }
    return {first, second};
}

constexpr WxcParams kFirstParams{.coefficients = {200, -60, 10}, .scale = 20, .mode = 0};

int sampleValue(const Wave& wave, std::size_t index) { return static_cast<int>(wave.samples.at(index) * 32768.0F); }

}  // namespace

TEST_CASE("WXC1 frames in cracked modes decode bit-exactly") {
    const auto wave = decodeWave(wxc1Blob(8000.0, kFirstParams, crackedFrames(), {1, 1}));
    REQUIRE(wave.has_value());
    REQUIRE(wave->samples.size() == 128);
    CHECK(wave->loopStart == 96);
    CHECK(wave->loopEnd == 128);
    CHECK(sampleValue(*wave, 0) == -15);
    CHECK(sampleValue(*wave, 7) == -35);
    CHECK(sampleValue(*wave, 59) == -84);
    CHECK(sampleValue(*wave, 95) == 83);
    CHECK(sampleValue(*wave, 96) == 151);
    CHECK(sampleValue(*wave, 101) == 368);
    CHECK(sampleValue(*wave, 119) == -513);
    CHECK(sampleValue(*wave, 127) == -740);
    CHECK(wave->looped());
}

TEST_CASE("WXC1 frames in the pair modes 4 and 7 decode bit-exactly") {
    std::vector<WxcFrame> frames = crackedFrames();
    frames[0].next.mode = 7;
    const auto wave = decodeWave(wxc1Blob(8000.0, {.coefficients = {200, -60, 10}, .scale = 20, .mode = 4}, frames));
    REQUIRE(wave.has_value());
    REQUIRE(wave->samples.size() == 224);
    CHECK(sampleValue(*wave, 0) == 54);
    CHECK(sampleValue(*wave, 7) == -60);
    CHECK(sampleValue(*wave, 59) == -124);
    CHECK(sampleValue(*wave, 95) == 126);
    CHECK(sampleValue(*wave, 96) == -141);
    CHECK(sampleValue(*wave, 101) == 60);
    CHECK(sampleValue(*wave, 119) == 229);
    CHECK(sampleValue(*wave, 127) == -486);
    CHECK(sampleValue(*wave, 223) == -924);
}

TEST_CASE("a WXC1 wave continues from the samples its header stores") {
    const WxcParams holdLast{.coefficients = {1024, 0, 0}, .scale = 0, .mode = 0};
    const auto wave = decodeWave(wxc1Blob(8000.0, holdLast, {WxcFrame{}}, {0xFFFFFFFFU, 0}, {1200, 900, 600}));
    REQUIRE(wave.has_value());
    REQUIRE(wave->samples.size() == 96);
    CHECK(sampleValue(*wave, 0) == 1200);
    CHECK(sampleValue(*wave, 95) == 1200);
}

TEST_CASE("a WXC1 frame in mode 5 decodes 64 samples, and a loop past the last frame plays as a one-shot") {
    std::vector<WxcFrame> frames = crackedFrames();
    frames.push_back(WxcFrame{});
    const auto wave = decodeWave(wxc1Blob(8000.0, kFirstParams, frames, {1, 3}));
    REQUIRE(wave.has_value());
    CHECK(wave->samples.size() == 192);
    CHECK_FALSE(wave->looped());
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

TEST_CASE("the store reads each element's envelope, low-pass filter and LFO in both voice flavors") {
    SyntheticStore store;
    const std::string wave = store.addWave(linearBlob(sine(440.0, 44100.0, 441), 44100.0));
    store.addPack(
        "Pack",
        {SyntheticVoice{.name = "Drum",
                        .zones = {SyntheticZone{.wave = wave}},
                        .blocks = "<amplitudeBlock><attackRate>0.534033</attackRate><decay1Rate>1.0</decay1Rate>"
                                  "<decay2Rate>0.367366</decay2Rate><releaseRate>0.534033</releaseRate>"
                                  "<attackLevel>0</attackLevel><decay1Level>0</decay1Level>"
                                  "<decay2Level>-48</decay2Level></amplitudeBlock>"
                                  "<filterBlock><dcfFilterType>LPF(YS5)</dcfFilterType><dcfCutoff>3000</dcfCutoff>"
                                  "</filterBlock><lfoBlock><frequency>4</frequency><amDepth>-6</amDepth></lfoBlock>"},
         SyntheticVoice{.name = "Pad",
                        .zones = {SyntheticZone{.wave = wave}},
                        .blocks = "<amplitudeBlock><attackTime>0.666667</attackTime><decay1Time>0.5</decay1Time>"
                                  "<decay2Time>0.833333</decay2Time><releaseTime>0.75</releaseTime>"
                                  "<attackLevel>0</attackLevel><decay1Level>-0.375</decay1Level>"
                                  "<decay2Level>-12</decay2Level></amplitudeBlock>"
                                  "<filterBlock><filterType>LPF</filterType><cutoffFrequency>1000</cutoffFrequency>"
                                  "<resonance>1.0</resonance></filterBlock>"
                                  "<lfoBlock><frequency>5</frequency><delayTime>250</delayTime>"
                                  "<pitchModulationDepth>20</pitchModulationDepth></lfoBlock>"},
         SyntheticVoice{.name = "Plain", .zones = {SyntheticZone{.wave = wave}}},
         SyntheticVoice{.name = "Wide",
                        .zones = {SyntheticZone{.wave = wave}},
                        .blocks = "<filterBlock><dcfFilterType>LPF(YS5)</dcfFilterType><dcfCutoff>7018.73</dcfCutoff>"
                                  "</filterBlock>"}});
    const Store loaded = loadStore(store.folder());
    REQUIRE(loaded.voices.size() == 4);
    CHECK_FALSE(loaded.voices[3].zones.at(0).lowPass.has_value());

    const Zone& drum = loaded.voices[0].zones.at(0);
    REQUIRE(drum.envelope.has_value());
    CHECK(drum.envelope->attackSeconds == Catch::Approx(1.0).epsilon(1e-4));
    CHECK(drum.envelope->decay1Seconds == 0.0);
    CHECK(drum.envelope->decay2Seconds == Catch::Approx(5.0).epsilon(1e-4));
    CHECK(drum.envelope->releaseSeconds == Catch::Approx(0.5).epsilon(1e-4));
    CHECK(drum.envelope->sustainGain == Catch::Approx(0.003981).epsilon(1e-3));
    REQUIRE(drum.lowPass.has_value());
    CHECK(drum.lowPass->hertz == Catch::Approx(3000.0));
    CHECK(drum.lfo.hertz == 4.0);
    CHECK(drum.lfo.amplitudeDb == -6.0);

    const Zone& pad = loaded.voices[1].zones.at(0);
    REQUIRE(pad.envelope.has_value());
    CHECK(pad.envelope->attackSeconds == Catch::Approx(1.0).epsilon(1e-4));
    CHECK(pad.envelope->decay1Seconds == Catch::Approx(0.1).epsilon(1e-4));
    CHECK(pad.envelope->decay2Seconds == Catch::Approx(10.0).epsilon(1e-4));
    CHECK(pad.envelope->releaseSeconds == Catch::Approx(3.1623).epsilon(1e-4));
    CHECK(pad.envelope->decay1Gain == Catch::Approx(0.95773).epsilon(1e-4));
    CHECK(pad.envelope->sustainGain == Catch::Approx(0.25119).epsilon(1e-4));
    REQUIRE(pad.lowPass.has_value());
    CHECK(pad.lowPass->hertz == 1000.0);
    CHECK(pad.lowPass->q == Catch::Approx(1.2589).epsilon(1e-4));
    CHECK(pad.lfo.delaySeconds == 0.25);
    CHECK(pad.lfo.pitchCents == 20.0);

    const Zone& plain = loaded.voices[2].zones.at(0);
    CHECK_FALSE(plain.envelope.has_value());
    CHECK_FALSE(plain.lowPass.has_value());
}

TEST_CASE("the store reads a legacy element's filter type, cutoff, curves and cutoff envelope") {
    SyntheticStore store;
    const std::string wave = store.addWave(linearBlob(sine(440.0, 44100.0, 441), 44100.0));
    const SyntheticLegacyFilter sweep{.type = "LPF_6DBOCT(YS5)",
                                      .cutoff = 27.48,
                                      .velocityCurve = ramp(-127, 0),
                                      .keyCurve = ramp(0, 10),
                                      .egDepth = 50,
                                      .holdTime = 250,
                                      .levels = {4950, 9450, 8000, 7000, 6000, 2000},
                                      .rates = {1.0, 0.534033, 0.534033, 0.534033, 0.534033}};
    store.addPack("Pack", {SyntheticVoice{.name = "Swept",
                                          .zones = {SyntheticZone{.wave = wave}},
                                          .blocks = legacyFilterBlock(sweep) + touchCurveBlock(ramp(-128, 0))},
                           SyntheticVoice{
                               .name = "Open", .zones = {SyntheticZone{.wave = wave}}, .blocks = legacyFilterBlock({})},
                           SyntheticVoice{.name = "Velocity",
                                          .zones = {SyntheticZone{.wave = wave}},
                                          .blocks = legacyFilterBlock({.velocityCurve = ramp(-64, 0)})}});
    const Store loaded = loadStore(store.folder());
    REQUIRE(loaded.voices.size() == 3);

    CHECK_FALSE(loaded.voices[0].zones.at(0).lowPass.has_value());

    const Zone& swept = loaded.voices[1].zones.at(0);
    REQUIRE(swept.lowPass.has_value());
    CHECK(swept.lowPass->hertz == Catch::Approx(27.48));
    CHECK(swept.lowPass->onePole);
    REQUIRE(swept.lowPass->velocityCents.has_value());
    CHECK(swept.lowPass->velocityCents->front() == Catch::Approx(-12700.0));
    CHECK(swept.lowPass->velocityCents->back() == 0.0);
    REQUIRE(swept.lowPass->keyCents.has_value());
    CHECK(swept.lowPass->keyCents->back() == Catch::Approx(1000.0));
    REQUIRE(swept.lowPass->envelope.has_value());
    const CutoffEnvelope& envelope = *swept.lowPass->envelope;
    CHECK(envelope.initialCents == Catch::Approx(2475.0));
    CHECK(envelope.holdSeconds == Catch::Approx(0.25));
    CHECK(envelope.segments[0].cents == Catch::Approx(4725.0));
    CHECK(envelope.segments[3].cents == Catch::Approx(3000.0));
    CHECK(envelope.segments[4].cents == Catch::Approx(1000.0));
    CHECK(envelope.segments[1].centsPerSecond == Catch::Approx(9600.0).epsilon(1e-4));
    REQUIRE(swept.velocityDb.has_value());
    CHECK(swept.velocityDb->front() == Catch::Approx(-48.0));
    CHECK(swept.velocityDb->back() == 0.0);

    const Zone& velocity = loaded.voices[2].zones.at(0);
    REQUIRE(velocity.lowPass.has_value());
    CHECK_FALSE(velocity.lowPass->onePole);
    CHECK(velocity.lowPass->hertz == 112300.0);
    CHECK_FALSE(velocity.lowPass->envelope.has_value());
    CHECK_FALSE(velocity.lowPass->keyCents.has_value());
    CHECK_FALSE(velocity.velocityDb.has_value());
}

TEST_CASE("the store leaves out zones whose waves are undecodable or missing, and voices left with none") {
    SyntheticStore store;
    const std::string linear = store.addWave(linearBlob(sine(440.0, 44100.0, 441), 44100.0));
    const std::string compressed = store.addWave(wxc1Blob(44100.0, kFirstParams, crackedFrames()));
    const std::string empty = store.addWave(wxc1Blob(44100.0, kFirstParams, {}));
    store.addPack("Pack",
                  {SyntheticVoice{.name = "Plays", .zones = {SyntheticZone{.wave = linear}}},
                   SyntheticVoice{.name = "Packed", .zones = {SyntheticZone{.wave = compressed, .waveType = "WXC"}}},
                   SyntheticVoice{.name = "Compressed",
                                  .zones = {SyntheticZone{.keyHi = "C3", .wave = linear},
                                            SyntheticZone{.keyLo = "C#3", .wave = empty}}},
                   SyntheticVoice{.name = "Missing", .zones = {SyntheticZone{.wave = std::string(40, 'f')}}}});
    const Store loaded = loadStore(store.folder());
    REQUIRE(loaded.voices.size() == 3);
    CHECK(loaded.voices[0].name == "Compressed");
    REQUIRE(loaded.voices[0].zones.size() == 1);
    CHECK(loaded.voices[0].zones[0].keyHi == 60);
    CHECK(loaded.voices[1].name == "Packed");
    CHECK(loaded.voices[2].name == "Plays");
}

TEST_CASE("a missing or empty store folder is an empty store") {
    CHECK(loadStore("/no/such/store").voices.empty());
    const SyntheticStore store;
    CHECK(loadStore(store.folder()).voices.empty());
}

TEST_CASE("the store reads whether a key takes note off, its alternate group and whether it plays at key off") {
    SyntheticStore store;
    const std::string wave = store.addWave(linearBlob(sine(440.0, 44100.0, 441), 44100.0));
    store.addPack("Kit", {SyntheticVoice{.name = "Hat",
                                         .msb = 127,
                                         .zones = {SyntheticZone{.wave = wave}},
                                         .drumSetup = "<alternateGroup>2</alternateGroup><rcvNoteOff>0</rcvNoteOff>"},
                          SyntheticVoice{.name = "Piano",
                                         .msb = 8,
                                         .zones = {SyntheticZone{.wave = wave}},
                                         .blocks = "<triggerBlock><keyOffSound>ON</keyOffSound></triggerBlock>"}});
    const Store loaded = loadStore(store.folder());
    REQUIRE(loaded.voices.size() == 2);
    const Zone& hat = loaded.voices[0].zones.at(0);
    CHECK_FALSE(hat.receivesNoteOff);
    CHECK(hat.alternateGroup == 2);
    const Zone& piano = loaded.voices[1].zones.at(0);
    CHECK(piano.receivesNoteOff);
    CHECK(piano.alternateGroup == 0);
    CHECK(piano.keyOff);
    CHECK_FALSE(hat.keyOff);
}
