#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <numbers>
#include <ranges>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "emerick/engine/plugin.hpp"
#include "emerick/engine/plugin_scan.hpp"
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
constexpr std::uint32_t kNoise = 6;
constexpr std::uint32_t kNoisySamples = 7;
using Message = std::array<std::uint8_t, 3>;

// Runs the main thread until the plugin publishes the store its thread read, which rescans the params.
void waitForStore(Plugin& instrument) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!instrument.takeParamsChanged()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        instrument.idle();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

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

    [[nodiscard]] std::shared_ptr<Plugin> instrument(const Json& state) const {
        auto created = createClapPlugin(EMERICK_VOICES_CLAP, "org.emerick.voices");
        REQUIRE(created.has_value());
        Json withFolder = state;
        withFolder["folder"] = store.folder().string();
        const std::string text = withFolder.dump();
        REQUIRE((*created)->loadState(std::vector<std::uint8_t>(text.begin(), text.end())));
        REQUIRE((*created)->activate(kRate, kBlock));
        waitForStore(**created);
        return *created;
    }
};

Json voiceNamed(const std::string& name, int program) {
    return Json{{"voice", {{"pack", "Test Pack"}, {"name", name}, {"msb", 8}, {"lsb", 0}, {"program", program}}}};
}

std::array<std::vector<float>, 2> renderStereo(Plugin& instrument, const std::vector<Message>& messages, int blocks) {
    for (const Message& message : messages) {
        instrument.queueMidi(message, 0);
    }
    std::array<std::vector<float>, 2> sides;
    for (int block = 0; block < blocks; ++block) {
        std::vector<float> left(kBlock);
        std::vector<float> right(kBlock);
        const std::array<float*, 2> mix{left.data(), right.data()};
        instrument.processBlock(mix, kBlock, 0, {});
        sides[0].insert(sides[0].end(), left.begin(), left.end());
        sides[1].insert(sides[1].end(), right.begin(), right.end());
    }
    return sides;
}

std::vector<float> render(Plugin& instrument, const std::vector<Message>& messages, int blocks) {
    return renderStereo(instrument, messages, blocks)[0];
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

// The longest run of samples within 0.1% of the loudest, which a predictor stuck at full scale leaves in a render.
std::size_t longestClampedRun(std::span<const float> samples) {
    const float peak = loudest(samples) * 0.999F;
    std::size_t longest = 0;
    std::size_t run = 0;
    for (const float sample : samples) {
        run = std::abs(sample) >= peak ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    return longest;
}

// A pack whose Bass voice decodes with noise below F3 and plays a clean 440 Hz sine from A3 up, and whose Layered Bass
// has a noisy soft layer with no clean zone of its own.
struct NoisyRig : Rig {
    NoisyRig() {
        const WxcParams runaway{.coefficients = {3000, -2946, 969}, .scale = 63, .mode = 0};
        const std::string noisy =
            store.addWave(wxc1Blob(44100.0, runaway, std::vector<WxcFrame>(64, WxcFrame{.next = runaway})));
        const std::string clean =
            store.addWave(linearBlob(sine(440.0, 44100.0, 44100), 44100.0, std::pair{0U, 44100U}));
        store.addPack(
            "Noisy Pack",
            {SyntheticVoice{.name = "Bass",
                            .zones = {SyntheticZone{.keyHi = "C1", .rootKey = "C0", .wave = noisy},
                                      SyntheticZone{.keyLo = "F1", .keyHi = "E3", .rootKey = "A1", .wave = noisy},
                                      SyntheticZone{.keyLo = "F3", .wave = clean}}},
             SyntheticVoice{.name = "Layered Bass",
                            .zones = {SyntheticZone{.keyHi = "E3", .velocityHi = 64, .wave = noisy},
                                      SyntheticZone{.velocityLo = 65, .wave = clean}}}});
    }
};

Json noisyVoice(const std::string& name) { return Json{{"voice", {{"pack", "Noisy Pack"}, {"name", name}}}}; }

}  // namespace

TEST_CASE("the expansion voices instrument loads through the CLAP host and lists its voices") {
    const auto described = describeClapBundle(EMERICK_VOICES_CLAP);
    REQUIRE(described.has_value());
    REQUIRE(described->size() == 1);
    CHECK(described->front().id == "org.emerick.voices");
    CHECK(described->front().name == "Expansion Voices");
    CHECK(std::ranges::contains(described->front().features, std::string{"instrument"}));

    const Rig rig;
    const std::vector<PluginParam> params = rig.instrument({})->params();
    REQUIRE(params.size() == 7);
    CHECK(params[0].name == "Voice");
    CHECK(params[0].enumerated);
    CHECK(params[0].choices == std::vector<std::string>{"None", "Test Pack / Fine", "Test Pack / Half Rate",
                                                        "Test Pack / Layers", "Test Pack / Short", "Test Pack / Sine"});
    CHECK(params[1].name == "Volume");
    CHECK(params[5].name == "Follow program changes");
    CHECK(params[5].choices == std::vector<std::string>{"Off", "On"});
    CHECK(params[6].name == "Noisy samples");
    CHECK(params[6].choices == std::vector<std::string>{"Use nearest clean sample", "Play as decoded"});
}

TEST_CASE("a noisy zone plays the nearest clean zone of its element, repitched to the note") {
    const NoisyRig rig;
    const auto instrument = rig.instrument(noisyVoice("Bass"));
    CHECK(instrument->paramText(kNoisySamples, *instrument->paramValue(kNoisySamples)) == "Use nearest clean sample");
    const std::vector<float> low = render(*instrument, {{0x90, 45, 100}}, 20);
    CHECK(std::abs(cents(frequency(low), 110.0)) < 2.0);
    CHECK(longestClampedRun(low) < 12);

    instrument->setParam(kNoisySamples, 1.0);
    render(*instrument, {{0x80, 45, 0}}, 100);
    CHECK(longestClampedRun(render(*instrument, {{0x90, 45, 100}}, 5)) > 48);
}

TEST_CASE("the noise warning names the notes that still decode with noise and how to fix them") {
    const NoisyRig rig;
    const auto bass = rig.instrument(noisyVoice("Bass"));
    CHECK(bass->paramText(kNoise, 0.0).empty());
    CHECK(*bass->paramValue(kNoise) == 0.0);
    bass->setParam(kNoisySamples, 1.0);
    CHECK(
        bass->paramText(kNoise, 0.0) ==
        "Choose another voice for C-2 to C1, F1 to E3, where this voice's samples decode with noise. Set Noisy samples "
        "to Use nearest clean sample.");
    CHECK(*bass->paramValue(kNoise) == 1.0);

    const auto layered = rig.instrument(noisyVoice("Layered Bass"));
    const std::string unfixable = "Choose another voice for C-2 to E3, where this voice's samples decode with noise";
    CHECK(layered->paramText(kNoise, 0.0) == unfixable);
    layered->setParam(kNoisySamples, 1.0);
    CHECK(layered->paramText(kNoise, 0.0) == unfixable);
    CHECK(layered->params().size() == 7);

    CHECK(rig.instrument(voiceNamed("Sine", 3))->paramText(kNoise, 0.0).empty());
}

TEST_CASE("the state keeps the Noisy samples choice, and an older state uses the nearest clean sample") {
    const NoisyRig rig;
    const auto instrument = rig.instrument(noisyVoice("Bass"));
    instrument->setParam(kNoisySamples, 1.0);
    const auto saved = instrument->saveState();
    REQUIRE(saved.has_value());
    CHECK(*rig.instrument(Json::parse(saved->begin(), saved->end()))->paramValue(kNoisySamples) == 1.0);
    CHECK(*rig.instrument(noisyVoice("Bass"))->paramValue(kNoisySamples) == 0.0);
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

TEST_CASE("a legacy voice darkens a note by its cutoff, filter type, velocity and cutoff envelope") {
    Rig rig;
    const std::string high =
        rig.store.addWave(linearBlob(sine(6000.0, 44100.0, 44100), 44100.0, std::pair{0U, 44100U}));
    const std::string flatTouch = touchCurveBlock(ramp(0, 0));
    const auto voice = [&](const char* name, int program, const std::string& blocks) {
        return SyntheticVoice{
            .name = name, .program = program, .zones = {SyntheticZone{.wave = high}}, .blocks = blocks};
    };
    rig.store.addPack(
        "Legacy", {voice("Open", 1, flatTouch),
                   voice("One Pole", 2, legacyFilterBlock({.type = "LPF_6DBOCT(YS5)", .cutoff = 1000.0})),
                   voice("Two Pole", 3, legacyFilterBlock({.cutoff = 1000.0})),
                   voice("Velocity", 4, legacyFilterBlock({.velocityCurve = ramp(-80, 0)}) + flatTouch),
                   voice("Swept", 5,
                         legacyFilterBlock({.levels = {-9600, 0, 0, 0, 0, 0}, .rates = {0.55, 1.0, 1.0, 1.0, 1.0}}))});
    const auto legacy = [&](const char* name, int program) {
        return rig.instrument(
            Json{{"voice", {{"pack", "Legacy"}, {"name", name}, {"msb", 0}, {"lsb", 0}, {"program", program}}}});
    };
    const auto held = [&](const char* name, int program, std::uint8_t velocity) {
        return loudest(std::span<const float>(render(*legacy(name, program), {{0x90, 69, velocity}}, 30)).last(4800));
    };

    const float open = held("Open", 1, 127);
    const float onePole = held("One Pole", 2, 127);
    CHECK(onePole < 0.25F * open);
    CHECK(held("Two Pole", 3, 127) < 0.5F * onePole);
    CHECK(held("Velocity", 4, 127) == Catch::Approx(open).epsilon(0.05));
    CHECK(held("Velocity", 4, 20) < 0.3F * open);

    const auto swept = legacy("Swept", 5);
    const float early = loudest(render(*swept, {{0x90, 69, 127}}, 5));
    CHECK(early < 0.1F * loudest(std::span<const float>(render(*swept, {}, 150)).last(4800)));
}

TEST_CASE("a legacy voice's touch curve sets the level of each velocity") {
    Rig rig;
    const std::string wave = rig.store.addWave(linearBlob(sine(440.0, 44100.0, 44100), 44100.0, std::pair{0U, 44100U}));
    rig.store.addPack("Touch", {SyntheticVoice{.name = "Touch",
                                               .program = 1,
                                               .zones = {SyntheticZone{.wave = wave}},
                                               .blocks = touchCurveBlock(ramp(-128, 0))}});
    const auto played = [&](std::uint8_t velocity) {
        const auto instrument = rig.instrument(
            Json{{"voice", {{"pack", "Touch"}, {"name", "Touch"}, {"msb", 0}, {"lsb", 0}, {"program", 1}}}});
        return loudest(std::span<const float>(render(*instrument, {{0x90, 69, velocity}}, 30)).last(4800));
    };
    CHECK(played(64) == Catch::Approx(played(127) * std::pow(10.0F, -63.0F * 0.375F / 20.0F)).epsilon(0.02));
}

TEST_CASE("the Volume param scales the output") {
    const Rig rig;
    const auto loud = rig.instrument(voiceNamed("Sine", 3));
    const auto quiet = rig.instrument(voiceNamed("Sine", 3));
    quiet->setParam(kVolume, -20.0);
    const float full = loudest(render(*loud, {{0x90, 69, 100}}, 10));
    CHECK(loudest(render(*quiet, {{0x90, 69, 100}}, 10)) == Catch::Approx(full * 0.1F).epsilon(0.02));
}

TEST_CASE("channel volume, expression and pan set a channel's level and side, and reset all controllers keeps volume") {
    const Rig rig;
    const auto steady = [](std::span<const float> samples) { return loudest(samples.last(4800)); };
    const auto instrument = rig.instrument(voiceNamed("Sine", 3));
    const float full = steady(render(*instrument, {{0x90, 69, 100}}, 20));
    CHECK(steady(render(*instrument, {{0xB0, 7, 50}}, 20)) == Catch::Approx(full * 0.25F).epsilon(0.02));
    const float expressed = 0.25F * std::pow(64.0F / 127.0F, 2.0F);
    CHECK(steady(render(*instrument, {{0xB0, 11, 64}}, 20)) == Catch::Approx(full * expressed).epsilon(0.02));
    CHECK(steady(render(*instrument, {{0xB0, 121, 0}}, 20)) == Catch::Approx(full * 0.25F).epsilon(0.02));

    const auto sides = renderStereo(*instrument, {{0xB0, 10, 0}}, 20);
    CHECK(steady(sides[0]) == Catch::Approx(full * 0.25F * std::numbers::sqrt2_v<float>).epsilon(0.02));
    CHECK(steady(sides[1]) < 1e-4F * full);
}

TEST_CASE("pitch bend retunes a sounding note by the bend range RPN 0 sets, and reset all controllers centers it") {
    const Rig rig;
    const auto instrument = rig.instrument(voiceNamed("Sine", 3));
    render(*instrument, {{0x90, 69, 100}}, 5);
    CHECK(cents(frequency(render(*instrument, {{0xE0, 0x7F, 0x7F}}, 20)), 440.0) == Catch::Approx(200.0).margin(2.0));
    const std::vector<Message> octave{{0xB0, 101, 0}, {0xB0, 100, 0}, {0xB0, 6, 12}};
    CHECK(cents(frequency(render(*instrument, octave, 20)), 440.0) == Catch::Approx(1200.0).margin(2.0));
    CHECK(std::abs(cents(frequency(render(*instrument, {{0xB0, 121, 0}}, 20)), 440.0)) < 2.0);
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

TEST_CASE("the store loads on its own thread and the state's voice plays once the main thread publishes it") {
    const Rig rig;
    const auto load = [&] {
        auto created = createClapPlugin(EMERICK_VOICES_CLAP, "org.emerick.voices");
        REQUIRE(created.has_value());
        Json state = voiceNamed("Sine", 3);
        state["folder"] = rig.store.folder().string();
        const std::string text = state.dump();
        REQUIRE((*created)->loadState(std::vector<std::uint8_t>(text.begin(), text.end())));
        return *created;
    };
    load().reset();

    const auto instrument = load();
    CHECK(instrument->params()[0].choices == std::vector<std::string>{"Loading voices"});
    CHECK(*instrument->paramValue(kVoice) == 0.0);
    REQUIRE(instrument->activate(kRate, kBlock));
    waitForStore(*instrument);
    CHECK(instrument->params()[0].choices.size() == 6);
    CHECK(instrument->paramText(kVoice, *instrument->paramValue(kVoice)) == "Test Pack / Sine");
    CHECK(std::abs(cents(frequency(render(*instrument, {{0x90, 69, 100}}, 20)), 440.0)) < 2.0);
}

TEST_CASE("a drum key that ignores note off plays out, and a key of its alternate group cuts it") {
    Rig rig;
    const std::string long440 = rig.store.addWave(linearBlob(sine(440.0, 44100.0, 44100), 44100.0));
    const std::string looped =
        rig.store.addWave(linearBlob(sine(440.0, 44100.0, 44100), 44100.0, std::pair{0U, 44100U}));
    const std::string click = rig.store.addWave(linearBlob(sine(440.0, 44100.0, 10), 44100.0));
    rig.store.addPack("Kit",
                      {SyntheticVoice{.name = "Kick",
                                      .msb = 127,
                                      .program = 1,
                                      .zones = {SyntheticZone{.wave = long440}},
                                      .drumSetup = "<rcvNoteOff>0</rcvNoteOff>"},
                       SyntheticVoice{.name = "Looped",
                                      .msb = 127,
                                      .program = 2,
                                      .zones = {SyntheticZone{.wave = looped}},
                                      .drumSetup = "<rcvNoteOff>0</rcvNoteOff>"},
                       SyntheticVoice{.name = "Hats",
                                      .msb = 127,
                                      .program = 3,
                                      .zones = {SyntheticZone{.keyLo = "C3", .keyHi = "C3", .wave = long440},
                                                SyntheticZone{.keyLo = "D3", .keyHi = "D3", .wave = click}},
                                      .drumSetup = "<rcvNoteOff>0</rcvNoteOff><alternateGroup>1</alternateGroup>"}});
    const auto kitVoice = [](const char* name, int program) {
        return Json{{"voice", {{"pack", "Kit"}, {"name", name}, {"msb", 127}, {"lsb", 0}, {"program", program}}}};
    };

    const auto kick = rig.instrument(kitVoice("Kick", 1));
    render(*kick, {{0x99, 69, 100}}, 1);
    CHECK(loudest(std::span<const float>(render(*kick, {{0x89, 69, 0}}, 20)).last(480)) > 0.1F);

    const auto loopedKey = rig.instrument(kitVoice("Looped", 2));
    render(*loopedKey, {{0x99, 69, 100}}, 1);
    CHECK(loudest(std::span<const float>(render(*loopedKey, {{0x89, 69, 0}}, 50)).last(480)) < 1e-6F);

    const auto hats = rig.instrument(kitVoice("Hats", 3));
    render(*hats, {{0x99, 60, 100}}, 5);
    CHECK(loudest(std::span<const float>(render(*hats, {{0x99, 62, 100}}, 5)).last(480)) < 1e-6F);
}

TEST_CASE("a drum kit plays each key's sample at its recorded pitch and never plays another key's for a noisy one") {
    Rig rig;
    const WxcParams runaway{.coefficients = {3000, -2946, 969}, .scale = 63, .mode = 0};
    const std::string noisy =
        rig.store.addWave(wxc1Blob(44100.0, runaway, std::vector<WxcFrame>(64, WxcFrame{.next = runaway})));
    const std::string a440 = rig.store.addWave(linearBlob(sine(440.0, 44100.0, 44100), 44100.0, std::pair{0U, 44100U}));
    rig.store.addPack(
        "Kit", {SyntheticVoice{.name = "Congas",
                               .msb = 127,
                               .zones = {SyntheticZone{.keyLo = "C1", .keyHi = "C1", .rootKey = "C1", .wave = noisy},
                                         SyntheticZone{.keyLo = "D1", .keyHi = "D1", .rootKey = "D1", .wave = a440},
                                         SyntheticZone{.keyLo = "C5", .keyHi = "C5", .rootKey = "C3", .wave = a440}},
                               .voiceType = "DRUM_KIT"}});
    const auto congas = rig.instrument(
        Json{{"voice", {{"pack", "Kit"}, {"name", "Congas"}, {"msb", 127}, {"lsb", 0}, {"program", 0}}}});
    CHECK(std::abs(cents(frequency(render(*congas, {{0x99, 84, 100}}, 20)), 440.0)) < 2.0);
    render(*congas, {{0x89, 84, 0}}, 100);
    CHECK(longestClampedRun(render(*congas, {{0x99, 36, 100}}, 5)) > 48);
}

TEST_CASE("the noise warning names the drum keys whose samples swell into a drone, and a melodic swell is no noise") {
    Rig rig;
    const WxcParams rising{.coefficients = {1024, 0, 0}, .scale = 17, .mode = 0};
    const std::string drone = rig.store.addWave(wxc1Blob(
        44100.0, rising, std::vector<WxcFrame>(96, WxcFrame{.next = rising}), {0xFFFFFFFFU, 0}, {-4000, -4004, -4008}));
    const std::string clean = rig.store.addWave(linearBlob(sine(440.0, 44100.0, 4410), 44100.0));
    rig.store.addPack(
        "Kit", {SyntheticVoice{.name = "Congas",
                               .msb = 127,
                               .zones = {SyntheticZone{.keyLo = "C5", .keyHi = "C5", .oneShot = true, .wave = drone},
                                         SyntheticZone{.keyLo = "D5", .keyHi = "D5", .oneShot = true, .wave = clean}},
                               .voiceType = "DRUM_KIT"},
                SyntheticVoice{.name = "Tuba", .msb = 8, .zones = {SyntheticZone{.oneShot = true, .wave = drone}}}});
    const auto kitVoice = [](const char* name, int msb) {
        return Json{{"voice", {{"pack", "Kit"}, {"name", name}, {"msb", msb}, {"lsb", 0}, {"program", 0}}}};
    };
    CHECK(rig.instrument(kitVoice("Congas", 127))->paramText(kNoise, 0.0) ==
          "Choose another voice for C5, where this voice's samples decode with noise");
    CHECK(rig.instrument(kitVoice("Tuba", 8))->paramText(kNoise, 0.0).empty());
}

TEST_CASE("a key-off element stays silent while the key is held and plays once when it is released") {
    Rig rig;
    const std::string clunk = rig.store.addWave(linearBlob(sine(440.0, 44100.0, 4410), 44100.0, std::pair{0U, 4410U}));
    rig.store.addPack("Keys", {SyntheticVoice{.name = "Clunk",
                                              .zones = {SyntheticZone{.wave = clunk}},
                                              .blocks = "<triggerBlock><keyOffSound>ON</keyOffSound></triggerBlock>"}});
    const auto instrument =
        rig.instrument(Json{{"voice", {{"pack", "Keys"}, {"name", "Clunk"}, {"msb", 0}, {"lsb", 0}, {"program", 0}}}});

    CHECK(loudest(render(*instrument, {{0x90, 69, 100}}, 5)) < 1e-6F);
    CHECK(loudest(render(*instrument, {{0x80, 69, 0}}, 2)) > 0.1F);
    CHECK(loudest(std::span<const float>(render(*instrument, {}, 40)).last(480)) < 1e-6F);
}
