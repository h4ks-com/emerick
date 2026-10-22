#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <random>
#include <span>
#include <string_view>
#include <vector>

#include "rig.hpp"
#include "rt_effects.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;
using Catch::Approx;

namespace {

EffectParams paramsWith(EffectType type, std::initializer_list<std::pair<std::string_view, float>> values) {
    const EffectSpec& spec = effectSpec(type);
    EffectParams params = defaultParams(spec);
    for (const auto& [id, value] : values) {
        params.at(*paramIndex(spec, id)) = value;
    }
    return params;
}

std::vector<float> sine(double frequency, Frames frames, float amplitude = 0.5F) {
    std::vector<float> samples(static_cast<std::size_t>(frames));
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] =
            amplitude *
            static_cast<float>(std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(index) / kRate));
    }
    return samples;
}

std::vector<float> impulse(Frames frames) {
    std::vector<float> samples(static_cast<std::size_t>(frames));
    samples.front() = 1.0F;
    return samples;
}

// Plays mono samples through the effect in blocks, switching to the later params at the frame change names.
std::vector<float> run(Effect& effect, std::vector<float> samples, const EffectParams& params, bool bypass = false,
                       Frames change = -1, const EffectParams& later = {}) {
    for (Frames at = 0; at < static_cast<Frames>(samples.size()); at += kBlock) {
        float* channel = samples.data() + at;
        const Frames frames = std::min(kBlock, static_cast<Frames>(samples.size()) - at);
        effect.process(std::span(&channel, 1), frames, change >= 0 && at >= change ? later : params, bypass, 120.0);
    }
    return samples;
}

std::vector<float> run(EffectType type, std::vector<float> samples, const EffectParams& params, bool bypass = false) {
    Effect effect{type, 1, kRate, kBlock};
    return run(effect, std::move(samples), params, bypass);
}

double rmsDb(std::span<const float> samples) {
    double sum = 0.0;
    for (const float sample : samples) {
        sum += static_cast<double>(sample) * sample;
    }
    return 10.0 * std::log10(sum / static_cast<double>(samples.size()));
}

double gainDb(const std::vector<float>& input, const std::vector<float>& output) {
    const std::size_t half = input.size() / 2;
    return rmsDb(std::span(output).subspan(half)) - rmsDb(std::span(input).subspan(half));
}

float largestStep(std::span<const float> samples) {
    float largest = 0.0F;
    for (std::size_t index = 1; index < samples.size(); ++index) {
        largest = std::max(largest, std::abs(samples[index] - samples[index - 1]));
    }
    return largest;
}

}  // namespace

TEST_CASE("an EQ peak band lifts its center frequency by its gain") {
    const EffectParams params = paramsWith(EffectType::eq, {{"peak2Freq", 1000}, {"peak2Gain", 6}, {"peak2Q", 1}});
    const std::vector<float> input = sine(1000, 48000);
    CHECK(gainDb(input, run(EffectType::eq, input, params)) == Approx(6.0).margin(0.1));
    CHECK(eqResponseDb(params, 1000, kRate) == Approx(6.0).margin(0.01));
    CHECK(eqResponseDb(params, 50, kRate) == Approx(0.0).margin(0.1));
}

TEST_CASE("an EQ low cut attenuates below its frequency by its slope and passes above it") {
    for (const auto& [slope, least] : {std::pair{0.0F, 36.0}, std::pair{3.0F, 120.0}}) {
        INFO(slope);
        const EffectParams params =
            paramsWith(EffectType::eq, {{"lowCutOn", 1}, {"lowCutFreq", 1000}, {"lowCutSlope", slope}});
        const std::vector<float> low = sine(100, 48000);
        CHECK(gainDb(low, run(EffectType::eq, low, params)) < -least);
        const std::vector<float> high = sine(8000, 48000);
        CHECK(gainDb(high, run(EffectType::eq, high, params)) == Approx(0.0).margin(0.2));
    }
}

TEST_CASE("a compressor follows its static curve above the threshold and its knee rounds the corner") {
    CHECK(compressedDb(-30, -20, 4, 0) == Approx(-30));
    CHECK(compressedDb(-12, -20, 4, 0) == Approx(-18));
    CHECK(compressedDb(-20, -20, 4, 8) == Approx(-20.75));
    CHECK(compressedDb(-24, -20, 4, 8) == Approx(-24));

    const EffectParams params =
        paramsWith(EffectType::compressor,
                   {{"threshold", -20}, {"ratio", 4}, {"knee", 0}, {"attack", 0.1F}, {"release", 5}, {"makeup", 0}});
    const std::vector<float> output = run(EffectType::compressor, std::vector<float>(24000, 0.5F), params);
    const double expectedDb = -20.0 + ((20.0 * std::log10(0.5) + 20.0) / 4.0);
    CHECK(20.0 * std::log10(output.back()) == Approx(expectedDb).margin(0.05));
}

TEST_CASE("auto gain makes up half the reduction a full scale signal would get") {
    const EffectParams params =
        paramsWith(EffectType::compressor, {{"threshold", -20}, {"ratio", 4}, {"knee", 0}, {"autoMakeup", 1}});
    const std::vector<float> output = run(EffectType::compressor, std::vector<float>(24000, 0.01F), params);
    CHECK(20.0 * std::log10(output.back() / 0.01) == Approx(7.5).margin(0.05));
}

TEST_CASE("a delay repeats its input after its time and each repeat falls by the feedback") {
    const EffectParams params =
        paramsWith(EffectType::delay, {{"sync", 0}, {"time", 100}, {"feedback", 50}, {"mix", 100}, {"highCut", 20000}});
    const std::vector<float> output = run(EffectType::delay, impulse(24000), params);
    CHECK(output.at(4799) == Approx(0.0).margin(1e-6));
    CHECK(output.at(4800) == Approx(1.0F));
    double second = 0.0;
    for (std::size_t index = 9500; index < 10000; ++index) {
        second += output.at(index);
    }
    CHECK(second == Approx(0.5).margin(0.01));
}

TEST_CASE("a synced delay plays its note value at the tempo") {
    const EffectParams params = paramsWith(EffectType::delay, {{"sync", 1}, {"note", 3}, {"mix", 100}});
    const std::vector<float> output = run(EffectType::delay, impulse(48000), params);
    CHECK(output.at(24000) == Approx(1.0F));
}

TEST_CASE("a reverb rings out in a tail that decays, and a bypassed reverb leaves the samples exactly as they were") {
    const EffectParams params = paramsWith(EffectType::reverb, {{"mix", 100}, {"decay", 1}, {"preDelay", 0}});
    const std::vector<float> output = run(EffectType::reverb, impulse(96000), params);
    const auto window = [&](double from) { return rmsDb(std::span(output).subspan(std::size_t(from * kRate), 4800)); };
    CHECK(std::isfinite(window(0.1)));
    CHECK(window(0.1) > window(0.5));
    CHECK(window(0.5) > window(1.0));
    CHECK(window(0.1) - window(1.0) == Approx(54.0).margin(15.0));

    std::mt19937 random{7};
    std::uniform_real_distribution<float> noise{-1.0F, 1.0F};
    std::vector<float> input(9600);
    for (float& sample : input) {
        sample = noise(random);
    }
    CHECK(run(EffectType::reverb, input, params, true) == input);
}

TEST_CASE("a param change glides, so a jump in make up gain never steps the signal") {
    const EffectParams quiet = paramsWith(EffectType::compressor, {{"threshold", 0}, {"ratio", 1}, {"makeup", 0}});
    const EffectParams loud = paramsWith(EffectType::compressor, {{"threshold", 0}, {"ratio", 1}, {"makeup", 24}});
    Effect effect{EffectType::compressor, 1, kRate, kBlock};
    const std::vector<float> output = run(effect, sine(1000, 24000, 0.05F), quiet, false, 4800, loud);
    const float loudAmplitude = 0.05F * std::pow(10.0F, 24.0F / 20.0F);
    CHECK(largestStep(output) < loudAmplitude * 2.0F * std::numbers::pi_v<float> * 1000.0F / kRate * 1.05F);
    CHECK(std::abs(output.back()) <= loudAmplitude * 1.01F);
    CHECK(rmsDb(std::span(output).subspan(19200)) == Approx(rmsDb(sine(1000, 4800, loudAmplitude))).margin(0.1));
}

TEST_CASE("turning bypass on crossfades to the dry signal and then passes it untouched") {
    const EffectParams params = paramsWith(EffectType::compressor, {{"threshold", 0}, {"ratio", 1}, {"makeup", 12}});
    Effect effect{EffectType::compressor, 1, kRate, kBlock};
    std::vector<float> input = sine(1000, 4800, 0.1F);
    run(effect, input, params);
    std::vector<float> fading = input;
    float* channel = fading.data();
    effect.process(std::span(&channel, 1), kBlock, params, true, 120.0);
    CHECK(largestStep(std::span(fading).first(kBlock)) <
          0.1F * 4.0F * 2.0F * std::numbers::pi_v<float> * 1000.0F / kRate);
    CHECK(run(effect, input, params, true) == input);
}

namespace {

struct EffectRig {
    TempFolder folder;
    std::filesystem::path burst = folder.path / "burst.wav";
    Rig rig;

    EffectRig() {
        std::vector<float> samples(48000);
        std::fill_n(samples.begin(), 480, 0.5F);
        writeWav(burst, samples);
    }

    Track track(std::vector<Insert> inserts) const {
        Track track{.id = "vox", .kind = TrackKind::audio};
        track.audioOut = {"keyboard", {1, 2}};
        track.audioClips = {{.path = burst, .start = 0, .length = 1920, .sourceOffset = 0, .gain = 1.0F}};
        track.inserts = std::move(inserts);
        return track;
    }

    std::vector<float> play(Frames frames) {
        const Frames start = rig.audio.processedFrames();
        rig.audio.process(frames);
        const auto output = rig.audio.output("keyboard", 1);
        return {output.begin() + start, output.end()};
    }
};

}  // namespace

TEST_CASE("inserts play before the fader, and a reverb's tail carries on when its params change") {
    EffectRig rig;
    const Insert verb{.id = "verb",
                      .type = EffectType::reverb,
                      .bypass = false,
                      .params = paramsWith(EffectType::reverb, {{"mix", 100}, {"decay", 4}, {"preDelay", 0}})};
    Track vox = rig.track({verb});
    vox.gain = 0.5F;
    rig.rig.engine.setSession({.tracks = {vox}});
    playOnceReadAhead(rig.rig);
    const std::vector<float> before = rig.play(9600);
    CHECK(rmsDb(std::span(before).last(960)) > -60.0);

    Insert edited = verb;
    edited.params = paramsWith(EffectType::reverb, {{"mix", 90}, {"decay", 4}, {"preDelay", 0}});
    vox.inserts = {edited};
    rig.rig.engine.setSession({.tracks = {vox}});
    const std::vector<float> after = rig.play(960);
    CHECK(rmsDb(after) > -60.0);

    Track fresh = vox;
    fresh.inserts = {Insert{.id = "other", .type = EffectType::reverb, .bypass = false, .params = edited.params}};
    rig.rig.engine.setSession({.tracks = {fresh}});
    CHECK(rmsDb(rig.play(960)) < -100.0);
}

TEST_CASE("a track's fader scales what its inserts made") {
    EffectRig rig;
    const Insert gain{.id = "gain",
                      .type = EffectType::compressor,
                      .bypass = false,
                      .params = paramsWith(EffectType::compressor, {{"threshold", 0}, {"ratio", 1}, {"makeup", 12}})};
    Track vox = rig.track({gain});
    vox.gain = 0.5F;
    rig.rig.engine.setSession({.tracks = {vox}});
    playOnceReadAhead(rig.rig);
    const std::vector<float> output = rig.play(kBlock * 4);
    CHECK(output.at(kBlock * 2) == Approx(0.5F * 0.5F * std::pow(10.0F, 12.0F / 20.0F)).margin(1e-3));
}

TEST_CASE("a compressor insert reports the deepest gain reduction since the last reading") {
    EffectRig rig;
    const Insert squash{.id = "squash",
                        .type = EffectType::compressor,
                        .bypass = false,
                        .params = paramsWith(EffectType::compressor,
                                             {{"threshold", -20}, {"ratio", 4}, {"knee", 0}, {"attack", 0.1F}})};
    rig.rig.engine.setSession({.tracks = {rig.track({squash})}});
    playOnceReadAhead(rig.rig);
    rig.play(kBlock * 4);
    const std::vector<GainReduction> readings = rig.rig.engine.takeGainReductions();
    REQUIRE(readings.size() == 1);
    CHECK(readings.front().insertId == "squash");
    CHECK(readings.front().db == Approx(-10.5).margin(0.5));
    CHECK(rig.rig.engine.takeGainReductions().front().db == 0.0F);
}
