#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <ranges>
#include <span>
#include <string>
#include <vector>

#include "emerick/engine/clap_instrument.hpp"
#include "emerick/engine/clap_scan.hpp"
#include "synthetic_store.hpp"

using namespace emerick::engine;
using namespace emerick::voices::test;
using Json = nlohmann::json;

namespace {

constexpr double kRate = 48000.0;
constexpr emerick::core::Frames kBlock = 480;
constexpr std::uint32_t kVoice = 0;
constexpr std::uint32_t kVolume = 1;
constexpr std::uint32_t kFollow = 5;
using Message = std::array<std::uint8_t, 3>;

// A pack of test voices: sines whose pitch and length say which zone and settings played them.
struct Rig {
    SyntheticStore store;

    Rig() {
        const std::string a440 = store.addWave(linearBlob(sine(440.0, 44100.0, 44100), 44100.0, std::pair{0U, 44100U}));
        const std::string halfRate =
            store.addWave(linearBlob(sine(440.0, 22050.0, 22050), 22050.0, std::pair{0U, 22050U}));
        const std::string e660 = store.addWave(linearBlob(sine(660.0, 44100.0, 44100), 44100.0, std::pair{0U, 44100U}));
        const std::string shortWave = store.addWave(linearBlob(sine(440.0, 44100.0, 4410), 44100.0));
        store.addPack(
            "Test Pack",
            {
                SyntheticVoice{
                    .name = "Sine", .msb = 8, .lsb = 0, .program = 3, .zones = {SyntheticZone{.wave = a440}}},
                SyntheticVoice{.name = "Half Rate",
                               .msb = 8,
                               .program = 4,
                               .zones = {SyntheticZone{.sampleRate = 22050.0, .wave = halfRate}}},
                SyntheticVoice{
                    .name = "Fine", .msb = 8, .program = 5, .zones = {SyntheticZone{.tuneFine = 50.0, .wave = a440}}},
                SyntheticVoice{.name = "Layers",
                               .msb = 8,
                               .program = 6,
                               .zones = {SyntheticZone{.velocityHi = 64, .wave = a440},
                                         SyntheticZone{.velocityLo = 65, .wave = e660}}},
                SyntheticVoice{.name = "Short",
                               .msb = 8,
                               .program = 7,
                               .zones = {SyntheticZone{.oneShot = true, .wave = shortWave}}},
            });
    }

    [[nodiscard]] std::shared_ptr<ClapInstrument> instrument(const Json& state) const {
        auto created = ClapInstrument::create(EMERICK_VOICES_CLAP, "org.emerick.voices");
        REQUIRE(created.has_value());
        Json withFolder = state;
        withFolder["folder"] = store.folder().string();
        const std::string text = withFolder.dump();
        REQUIRE((*created)->loadState(std::vector<std::uint8_t>(text.begin(), text.end())));
        REQUIRE((*created)->activate(kRate, kBlock));
        return *created;
    }
};

Json voiceNamed(const std::string& name, int program) {
    return Json{{"voice", {{"pack", "Test Pack"}, {"name", name}, {"msb", 8}, {"lsb", 0}, {"program", program}}}};
}

std::vector<float> render(ClapInstrument& instrument, const std::vector<Message>& messages, int blocks) {
    for (const Message& message : messages) {
        instrument.queueMidi(message, 0);
    }
    std::vector<float> left;
    for (int block = 0; block < blocks; ++block) {
        std::vector<float> chunk(kBlock);
        std::vector<float> right(kBlock);
        const std::array<float*, 2> mix{chunk.data(), right.data()};
        instrument.processBlock(mix, kBlock, 0);
        left.insert(left.end(), chunk.begin(), chunk.end());
    }
    return left;
}

float loudest(std::span<const float> samples) {
    return std::ranges::max(samples | std::views::transform([](float sample) { return std::abs(sample); }));
}

// The frequency from the first to the last upward zero crossing, interpolated between samples.
double frequency(std::span<const float> samples) {
    std::vector<double> crossings;
    for (std::size_t index = 1; index < samples.size(); ++index) {
        if (samples[index - 1] < 0.0F && samples[index] >= 0.0F) {
            const double fraction = samples[index - 1] / (samples[index - 1] - samples[index]);
            crossings.push_back(static_cast<double>(index - 1) + fraction);
        }
    }
    REQUIRE(crossings.size() > 2);
    return static_cast<double>(crossings.size() - 1) * kRate / (crossings.back() - crossings.front());
}

double cents(double played, double wanted) { return 1200.0 * std::log2(played / wanted); }

}  // namespace

TEST_CASE("the expansion voices instrument loads through the CLAP host and lists its voices") {
    const auto described = describeClapBundle(EMERICK_VOICES_CLAP);
    REQUIRE(described.has_value());
    REQUIRE(described->size() == 1);
    CHECK(described->front().id == "org.emerick.voices");
    CHECK(described->front().name == "Expansion Voices");
    CHECK(std::ranges::contains(described->front().features, std::string{"instrument"}));

    const Rig rig;
    const std::vector<ClapParam> params = rig.instrument({})->params();
    REQUIRE(params.size() == 6);
    CHECK(params[0].name == "Voice");
    CHECK(params[0].enumerated);
    CHECK(params[0].choices == std::vector<std::string>{"None", "Test Pack / Fine", "Test Pack / Half Rate",
                                                        "Test Pack / Layers", "Test Pack / Short", "Test Pack / Sine"});
    CHECK(params[1].name == "Volume");
    CHECK(params[5].name == "Follow program changes");
    CHECK(params[5].choices == std::vector<std::string>{"Off", "On"});
}

TEST_CASE("a state that names a voice only by its bank and program picks the voice at that address") {
    const Rig rig;
    const auto picked = [&](int program) {
        const Json state{{"voice", {{"pack", ""}, {"name", ""}, {"msb", 8}, {"lsb", 0}, {"program", program}}}};
        return rig.instrument(state)->paramValue(kVoice);
    };
    CHECK(picked(4) == 2.0);
    CHECK(picked(99) == 0.0);
}

TEST_CASE("a note plays its zone at the pitch its root key, sample rate and fine tune give") {
    const Rig rig;
    const auto pitch = [&](const Json& state, int key) {
        return frequency(render(*rig.instrument(state), {{0x90, static_cast<std::uint8_t>(key), 100}}, 20));
    };
    CHECK(std::abs(cents(pitch(voiceNamed("Sine", 3), 69), 440.0)) < 2.0);
    CHECK(std::abs(cents(pitch(voiceNamed("Sine", 3), 81), 880.0)) < 2.0);
    CHECK(std::abs(cents(pitch(voiceNamed("Sine", 3), 64), 329.63)) < 2.0);
    CHECK(std::abs(cents(pitch(voiceNamed("Half Rate", 4), 69), 440.0)) < 2.0);
    CHECK(std::abs(cents(pitch(voiceNamed("Fine", 5), 69), 440.0) - 50.0) < 2.0);
}

TEST_CASE("velocity picks the zone that covers it") {
    const Rig rig;
    const auto pitch = [&](int velocity) {
        return frequency(
            render(*rig.instrument(voiceNamed("Layers", 6)), {{0x90, 69, static_cast<std::uint8_t>(velocity)}}, 20));
    };
    CHECK(std::abs(cents(pitch(40), 440.0)) < 2.0);
    CHECK(std::abs(cents(pitch(120), 660.0)) < 2.0);
}

TEST_CASE("a looped zone sounds while held, a one-shot stops at its end and a release fades out") {
    const Rig rig;
    const auto looped = rig.instrument(voiceNamed("Sine", 3));
    const std::vector<float> held = render(*looped, {{0x90, 69, 100}}, 200);
    CHECK(loudest(std::span(held).last(4800)) > 0.1F);
    const std::vector<float> released = render(*looped, {{0x80, 69, 0}}, 50);
    CHECK(loudest(std::span(released).first(480)) > 0.1F);
    CHECK(loudest(std::span(released).last(480)) < 1e-6F);

    const auto oneShot = rig.instrument(voiceNamed("Short", 7));
    const std::vector<float> played = render(*oneShot, {{0x90, 69, 100}}, 30);
    CHECK(loudest(std::span(played).first(2400)) > 0.1F);
    CHECK(loudest(std::span(played).last(4800)) < 1e-6F);
}

TEST_CASE("a voice plays its element's envelope and low-pass filter") {
    Rig rig;
    const std::string wave = rig.store.addWave(linearBlob(sine(440.0, 44100.0, 44100), 44100.0, std::pair{0U, 44100U}));
    rig.store.addPack(
        "Shaped",
        {SyntheticVoice{.name = "Quiet",
                        .program = 1,
                        .zones = {SyntheticZone{.wave = wave}},
                        .blocks = "<amplitudeBlock><attackTime>0</attackTime><decay1Time>0</decay1Time>"
                                  "<decay2Time>0.5</decay2Time><releaseTime>0.5</releaseTime>"
                                  "<decay2Level>-20</decay2Level></amplitudeBlock>"},
         SyntheticVoice{.name = "Dark",
                        .program = 2,
                        .zones = {SyntheticZone{.wave = wave}},
                        .blocks = "<filterBlock><filterType>LPF</filterType><cutoffFrequency>100</cutoffFrequency>"
                                  "<resonance>0</resonance></filterBlock>"}});
    const auto shaped = [&](const std::string& name, int program) {
        return rig.instrument(
            Json{{"voice", {{"pack", "Shaped"}, {"name", name}, {"msb", 0}, {"lsb", 0}, {"program", program}}}});
    };
    const float full = loudest(
        std::span<const float>(render(*rig.instrument(voiceNamed("Sine", 3)), {{0x90, 69, 100}}, 30)).last(4800));

    const auto quiet = shaped("Quiet", 1);
    const std::vector<float> held = render(*quiet, {{0x90, 69, 100}}, 30);
    CHECK(loudest(std::span(held).first(48)) > 0.5F * full);
    CHECK(loudest(std::span(held).last(4800)) == Catch::Approx(0.1F * full).epsilon(0.05));
    CHECK(loudest(std::span<const float>(render(*quiet, {{0x80, 69, 0}}, 30)).last(480)) < 1e-6F);

    CHECK(loudest(std::span<const float>(render(*shaped("Dark", 2), {{0x90, 69, 100}}, 30)).last(4800)) < 0.1F * full);
}

TEST_CASE("the Volume param scales the output") {
    const Rig rig;
    const auto loud = rig.instrument(voiceNamed("Sine", 3));
    const auto quiet = rig.instrument(voiceNamed("Sine", 3));
    quiet->setParam(kVolume, -20.0);
    const float full = loudest(render(*loud, {{0x90, 69, 100}}, 10));
    CHECK(loudest(render(*quiet, {{0x90, 69, 100}}, 10)) == Catch::Approx(full * 0.1F).epsilon(0.02));
}

TEST_CASE("program changes pick the voice at that bank address while the instrument follows them") {
    const Rig rig;
    const auto instrument = rig.instrument(voiceNamed("Sine", 3));
    render(*instrument, {{0xB0, 0, 8}, {0xB0, 32, 0}, {0xC0, 6, 0}}, 1);
    CHECK(instrument->paramText(kVoice, *instrument->paramValue(kVoice)) == "Test Pack / Sine");

    instrument->setParam(kFollow, 1.0);
    render(*instrument, {{0xB0, 0, 8}, {0xB0, 32, 0}, {0xC0, 6, 0}}, 1);
    CHECK(instrument->takeParamsChanged());
    instrument->idle();
    CHECK(instrument->paramText(kVoice, *instrument->paramValue(kVoice)) == "Test Pack / Layers");
    CHECK(std::abs(cents(frequency(render(*instrument, {{0x90, 69, 120}}, 20)), 660.0)) < 2.0);

    render(*instrument, {{0x80, 69, 0}, {0xB0, 0, 99}, {0xC0, 6, 0}}, 50);
    CHECK(*instrument->paramValue(kVoice) == 0.0);
    CHECK(loudest(render(*instrument, {{0x90, 69, 100}}, 10)) < 1e-6F);
}

TEST_CASE("the playing voice keeps sounding while a program change waits for its new voice to decode") {
    const Rig rig;
    const auto instrument = rig.instrument(voiceNamed("Sine", 3));
    instrument->setParam(kFollow, 1.0);
    const std::vector<float> waiting =
        render(*instrument, {{0xB0, 0, 8}, {0xB0, 32, 0}, {0xC0, 6, 0}, {0x90, 69, 120}}, 20);
    CHECK(std::abs(cents(frequency(waiting), 440.0)) < 2.0);

    instrument->idle();
    render(*instrument, {{0x80, 69, 0}}, 50);
    CHECK(std::abs(cents(frequency(render(*instrument, {{0x90, 69, 120}}, 20)), 660.0)) < 2.0);
}

TEST_CASE("the state names the voice by pack, name and address, so a changed store keeps it") {
    Rig rig;
    const Json state = [&] {
        const auto instrument = rig.instrument(voiceNamed("Sine", 3));
        instrument->setParam(kVolume, -6.0);
        const auto saved = instrument->saveState();
        REQUIRE(saved.has_value());
        return Json::parse(saved->begin(), saved->end());
    }();
    CHECK(state.at("voice") == voiceNamed("Sine", 3).at("voice"));
    CHECK(state.at("volume") == -6.0);

    const std::string wave = rig.store.addWave(linearBlob(sine(440.0, 44100.0, 4410), 44100.0));
    rig.store.addPack("Another Pack", {SyntheticVoice{.name = "Alpha", .zones = {SyntheticZone{.wave = wave}}}});
    const auto reloaded = rig.instrument(state);
    CHECK(reloaded->params()[0].choices[1] == "Another Pack / Alpha");
    CHECK(reloaded->paramText(kVoice, *reloaded->paramValue(kVoice)) == "Test Pack / Sine");
    CHECK(*reloaded->paramValue(kVolume) == -6.0);

    const auto missing = rig.instrument(voiceNamed("Gone", 9));
    CHECK(*missing->paramValue(kVoice) == 0.0);
    const auto kept = missing->saveState();
    REQUIRE(kept.has_value());
    CHECK(Json::parse(kept->begin(), kept->end()).at("voice") == voiceNamed("Gone", 9).at("voice"));
}
