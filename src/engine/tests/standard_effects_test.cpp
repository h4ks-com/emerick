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

TEST_CASE("an amp turns silence into silence even at full gain") {
    const Channels silence{std::vector<float>(4800, 0.0F), std::vector<float>(4800, 0.0F)};
    const EffectParams loud = paramsWith(EffectType::amp, {{"gain", 10}, {"master", 10}, {"model", 2}});
    CHECK(run(EffectType::amp, silence, loud) == silence);
}

TEST_CASE("an amp keeps a quiet sine nearly clean at low gain and adds odd and even harmonics at high gain") {
    const std::vector<float> input = sine(200, 48000, 0.1F);
    const auto harmonicsDb = [&](float gain) {
        const std::vector<float> output = runMono(
            EffectType::amp, input, paramsWith(EffectType::amp, {{"gain", gain}, {"model", 2}, {"cabinet", 0}}));
        const double fundamental = toneDb(secondHalf(output), 200);
        return std::pair{toneDb(secondHalf(output), 400) - fundamental, toneDb(secondHalf(output), 600) - fundamental};
    };
    const auto [cleanEven, cleanOdd] = harmonicsDb(0);
    CHECK(cleanEven < -30.0);
    CHECK(cleanOdd < -30.0);
    const auto [drivenEven, drivenOdd] = harmonicsDb(10);
    CHECK(drivenEven > -30.0);
    CHECK(drivenOdd > -20.0);
}

TEST_CASE("an amp's cabinet rolls 10 kHz off well below 1 kHz") {
    const auto spreadDb = [](float cabinet) {
        const EffectParams params = paramsWith(EffectType::amp, {{"gain", 0}, {"cabinet", cabinet}});
        const std::vector<float> low = sine(1000, 24000, 0.05F);
        const std::vector<float> high = sine(10000, 24000, 0.05F);
        return gainDb(high, runMono(EffectType::amp, high, params)) -
               gainDb(low, runMono(EffectType::amp, low, params));
    };
    const double open = spreadDb(0);
    for (const float cabinet : {1.0F, 2.0F, 3.0F}) {
        INFO(cabinet);
        CHECK(spreadDb(cabinet) < open - 20.0);
    }
}

TEST_CASE("each amp model shapes its frequency response its own way") {
    const auto responseDb = [](float model) {
        std::vector<double> response;
        const EffectParams params = paramsWith(EffectType::amp, {{"gain", 0}, {"cabinet", 0}, {"model", model}});
        const auto levelDb = [&](double frequency) {
            const std::vector<float> input = sine(frequency, 24000, 0.02F);
            return gainDb(input, runMono(EffectType::amp, input, params));
        };
        // We compare the shape around 400 Hz, since each model's drive also sets its overall level.
        const double reference = levelDb(400.0);
        for (const double frequency : {100.0, 1000.0, 3000.0}) {
            response.push_back(levelDb(frequency) - reference);
        }
        return response;
    };
    const std::vector<std::vector<double>> models{responseDb(0), responseDb(1), responseDb(2)};
    for (std::size_t first = 0; first < models.size(); ++first) {
        for (std::size_t second = first + 1; second < models.size(); ++second) {
            INFO(first << " " << second);
            double largest = 0.0;
            for (std::size_t point = 0; point < models[first].size(); ++point) {
                largest = std::max(largest, std::abs(models[first][point] - models[second][point]));
            }
            CHECK(largest > 1.0);
        }
    }
}

TEST_CASE("an amp stays finite at extreme params") {
    std::vector<float> input = sine(110, 9600, 4.0F);
    input.at(100) = 50.0F;
    for (const float model : {0.0F, 1.0F, 2.0F}) {
        for (const float cabinet : {0.0F, 3.0F}) {
            for (const float knobs : {0.0F, 10.0F}) {
                const EffectParams params = paramsWith(EffectType::amp, {{"model", model},
                                                                         {"cabinet", cabinet},
                                                                         {"gain", knobs},
                                                                         {"bass", knobs},
                                                                         {"mids", knobs},
                                                                         {"treble", knobs},
                                                                         {"presence", knobs},
                                                                         {"master", knobs},
                                                                         {"output", knobs > 0 ? 12.0F : -24.0F}});
                const std::vector<float> output = runMono(EffectType::amp, input, params);
                CHECK(std::ranges::all_of(output, [](float sample) { return std::isfinite(sample); }));
                CHECK(std::ranges::max(output) < 100.0F);
            }
        }
    }
}

TEST_CASE("an amp plays the same signal on both channels to the same output") {
    const std::vector<float> guitar = sine(196, 9600, 0.3F);
    const Channels output = run(EffectType::amp, {guitar, guitar}, paramsWith(EffectType::amp, {{"gain", 8}}));
    CHECK(output.at(0) == output.at(1));
    CHECK(output.at(0) != guitar);
}
