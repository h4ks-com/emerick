#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>
#include <functional>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

#include "rig.hpp"
#include "rt_effects.hpp"

using namespace emerick::engine;
using namespace emerick::engine::test;
using Catch::Approx;

namespace {

using Channels = std::vector<std::vector<float>>;

EffectParams paramsWith(EffectType type, std::initializer_list<std::pair<std::string_view, float>> values) {
    const EffectSpec& spec = effectSpec(type);
    EffectParams params = defaultParams(spec);
    for (const auto& [id, value] : values) {
        params.at(*paramIndex(spec, id)) = value;
    }
    return params;
}

std::vector<float> sine(double frequency, Frames frames, float amplitude = 0.5F, double phase = 0.0) {
    std::vector<float> samples(static_cast<std::size_t>(frames));
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] =
            amplitude * static_cast<float>(std::sin(
                            (2.0 * std::numbers::pi * frequency * static_cast<double>(index) / kRate) + phase));
    }
    return samples;
}

Channels run(EffectType type, Channels channels, const EffectParams& params) {
    Effect effect{type, channels.size(), kRate, kBlock};
    std::vector<float*> pointers(channels.size());
    const auto length = static_cast<Frames>(channels.front().size());
    for (Frames at = 0; at < length; at += kBlock) {
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            pointers[channel] = channels[channel].data() + at;
        }
        effect.process(pointers, std::min(kBlock, length - at), params, false, 120.0);
    }
    return channels;
}

std::vector<float> runMono(EffectType type, const std::vector<float>& samples, const EffectParams& params) {
    return run(type, {samples}, params).front();
}

std::span<const float> secondHalf(const std::vector<float>& samples) {
    return std::span(samples).subspan(samples.size() / 2);
}

double rmsDb(std::span<const float> samples) {
    double sum = 0.0;
    for (const float sample : samples) {
        sum += static_cast<double>(sample) * sample;
    }
    return 10.0 * std::log10(sum / static_cast<double>(samples.size()));
}

double gainDb(const std::vector<float>& input, const std::vector<float>& output) {
    return rmsDb(secondHalf(output)) - rmsDb(secondHalf(input));
}

double toneDb(std::span<const float> samples, double frequency) {
    std::complex<double> sum;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        sum += static_cast<double>(samples[index]) *
               std::polar(1.0, -2.0 * std::numbers::pi * frequency * static_cast<double>(index) / kRate);
    }
    return 20.0 * std::log10(std::abs(sum) * 2.0 / static_cast<double>(samples.size()) + 1e-12);
}

}  // namespace

TEST_CASE("a gate closes by its range below the threshold and passes a signal above it") {
    const EffectParams params = paramsWith(EffectType::gate, {{"threshold", -20}, {"range", 80}});
    const std::vector<float> quiet = sine(1000, 48000, 0.01F);
    CHECK(gainDb(quiet, runMono(EffectType::gate, quiet, params)) < -75.0);
    const std::vector<float> loud = sine(1000, 48000, 0.5F);
    CHECK(gainDb(loud, runMono(EffectType::gate, loud, params)) == Approx(0.0).margin(0.01));
}

TEST_CASE("a low pass filter attenuates a 10 kHz sine and passes 100 Hz, more steeply at 24 dB") {
    for (const auto& [slope, least] : {std::pair{0.0F, 30.0}, std::pair{1.0F, 60.0}}) {
        INFO(slope);
        const EffectParams params = paramsWith(EffectType::filter, {{"mode", 0}, {"cutoff", 1000}, {"slope", slope}});
        const std::vector<float> high = sine(10000, 48000);
        CHECK(gainDb(high, runMono(EffectType::filter, high, params)) < -least);
        const std::vector<float> low = sine(100, 48000);
        CHECK(gainDb(low, runMono(EffectType::filter, low, params)) == Approx(0.0).margin(0.1));
    }
}

TEST_CASE("a filter's cutoff jump glides, so a sweep never steps the signal") {
    const EffectParams closed = paramsWith(EffectType::filter, {{"cutoff", 100}});
    const EffectParams open = paramsWith(EffectType::filter, {{"cutoff", 15000}});
    Effect effect{EffectType::filter, 1, kRate, kBlock};
    std::vector<float> samples = sine(3000, 24000);
    for (Frames at = 0; at < static_cast<Frames>(samples.size()); at += kBlock) {
        float* channel = samples.data() + at;
        effect.process(std::span(&channel, 1), kBlock, at < 4800 ? closed : open, false, 120.0);
    }
    float largest = 0.0F;
    for (std::size_t index = 1; index < samples.size(); ++index) {
        largest = std::max(largest, std::abs(samples[index] - samples[index - 1]));
    }
    CHECK(largest < 0.5F * 2.0F * std::numbers::pi_v<float> * 3000.0F / kRate * 1.1F);
}

TEST_CASE("a high pass filter attenuates 100 Hz, and resonance lifts the cutoff") {
    const std::vector<float> low = sine(100, 48000);
    const EffectParams highPass = paramsWith(EffectType::filter, {{"mode", 1}, {"cutoff", 1000}});
    CHECK(gainDb(low, runMono(EffectType::filter, low, highPass)) < -30.0);
    const std::vector<float> atCutoff = sine(1000, 48000);
    const EffectParams resonant = paramsWith(EffectType::filter, {{"cutoff", 1000}, {"resonance", 50}});
    CHECK(gainDb(atCutoff, runMono(EffectType::filter, atCutoff, resonant)) > 6.0);
}

TEST_CASE("saturation adds harmonics to a sine, and tube adds even ones") {
    const std::vector<float> input = sine(1000, 48000);
    CHECK(toneDb(secondHalf(input), 3000) < -100.0);
    const std::vector<float> soft =
        runMono(EffectType::saturation, input, paramsWith(EffectType::saturation, {{"drive", 18}}));
    CHECK(toneDb(secondHalf(soft), 3000) - toneDb(secondHalf(soft), 1000) > -30.0);
    const std::vector<float> tube =
        runMono(EffectType::saturation, input, paramsWith(EffectType::saturation, {{"drive", 18}, {"character", 2}}));
    CHECK(toneDb(secondHalf(tube), 2000) - toneDb(secondHalf(tube), 1000) > -40.0);
}

TEST_CASE("a chorus changes the signal at a mix above 0 and leaves it exactly as it was at mix 0") {
    const Channels input{sine(440, 24000), sine(440, 24000)};
    const Channels wet = run(EffectType::chorus, input, paramsWith(EffectType::chorus, {{"mix", 50}}));
    CHECK(wet != input);
    CHECK(wet.at(0) != wet.at(1));
    CHECK(run(EffectType::chorus, input, paramsWith(EffectType::chorus, {{"mix", 0}})) == input);
}

TEST_CASE("utility at width 0 makes both channels equal, and inverting flips each channel's sign") {
    const Channels input{sine(440, 4800), sine(660, 4800, 0.3F, 1.0)};
    const Channels mono = run(EffectType::utility, input, paramsWith(EffectType::utility, {{"width", 0}}));
    CHECK(mono.at(0) == mono.at(1));
    CHECK(run(EffectType::utility, input, paramsWith(EffectType::utility, {{"mono", 1}})).at(0) == mono.at(0));

    const Channels flipped =
        run(EffectType::utility, input, paramsWith(EffectType::utility, {{"invertLeft", 1}, {"invertRight", 1}}));
    const auto negated = [](std::vector<float> samples) {
        std::ranges::transform(samples, samples.begin(), std::negate{});
        return samples;
    };
    CHECK(flipped.at(0) == negated(input.at(0)));
    CHECK(flipped.at(1) == negated(input.at(1)));
    const Channels left = run(EffectType::utility, input, paramsWith(EffectType::utility, {{"invertLeft", 1}}));
    CHECK(left.at(1) == input.at(1));
}

TEST_CASE("utility gain and balance scale each channel") {
    const Channels input{std::vector<float>(4800, 0.5F), std::vector<float>(4800, 0.5F)};
    const Channels output =
        run(EffectType::utility, input, paramsWith(EffectType::utility, {{"gain", -6}, {"balance", 50}}));
    CHECK(output.at(0).back() == Approx(0.5F * std::pow(10.0F, -6.0F / 20.0F) * 0.5F));
    CHECK(output.at(1).back() == Approx(0.5F * std::pow(10.0F, -6.0F / 20.0F)));
}
