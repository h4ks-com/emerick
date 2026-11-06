#include "emerick/engine/audio_analysis.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

using Catch::Approx;
using emerick::engine::analyzeAudio;
using emerick::engine::analyzeTone;
using emerick::engine::kFrequencyBands;

namespace {

constexpr double kRate = 48000;

std::vector<float> sine(double hz, double amplitude, double seconds, double phase = 0) {
    std::vector<float> samples(static_cast<std::size_t>(seconds * kRate));
    for (std::size_t frame = 0; frame < samples.size(); ++frame) {
        samples[frame] = static_cast<float>(
            amplitude * std::sin((2 * std::numbers::pi * hz * static_cast<double>(frame) / kRate) + phase));
    }
    return samples;
}

emerick::engine::SignalReplay replayInChunks(std::vector<std::vector<float>> channels, std::size_t chunkFrames) {
    return [channels = std::move(channels), chunkFrames](const emerick::engine::ChunkSink& sink) {
        for (std::size_t first = 0; first < channels.front().size(); first += chunkFrames) {
            const std::size_t last = std::min(first + chunkFrames, channels.front().size());
            sink(channels | std::views::transform([&](const std::vector<float>& channel) {
                     return std::vector<float>(channel.begin() + static_cast<std::ptrdiff_t>(first),
                                               channel.begin() + static_cast<std::ptrdiff_t>(last));
                 }) |
                 std::ranges::to<std::vector>());
        }
    };
}

std::vector<std::vector<float>> limitedSignal(const emerick::engine::SignalReplay& signal, double gainDb,
                                              double ceilingDbtp) {
    std::vector<std::vector<float>> out(2);
    emerick::engine::limitSignal(
        signal, 2, kRate, gainDb, ceilingDbtp, [&out](std::span<const std::vector<float>> chunk) {
            for (std::size_t channel = 0; channel < out.size(); ++channel) {
                out[channel].insert(out[channel].end(), chunk[channel].begin(), chunk[channel].end());
            }
        });
    return out;
}

}  // namespace

TEST_CASE("a full scale 997 Hz sine reads -3.01 LUFS and a 1 kHz sine at -20 dBFS about -23 LUFS") {
    CHECK(analyzeAudio({sine(997, 1, 3)}, {.sampleRate = kRate}).levels.integratedLufs == Approx(-3.01).margin(0.05));
    CHECK(analyzeAudio({sine(1000, 0.1, 3)}, {.sampleRate = kRate}).levels.integratedLufs ==
          Approx(-23.01).margin(0.1));
}

TEST_CASE("true peak finds the crest between samples that the sample peak misses") {
    const auto analysis = analyzeAudio({sine(kRate / 4, 1, 1, std::numbers::pi / 4)}, {.sampleRate = kRate});
    CHECK(analysis.levels.samplePeakDbfs == Approx(-3.01).margin(0.05));
    CHECK(analysis.levels.truePeakDbtp == Approx(0).margin(0.3));
}

TEST_CASE("bar levels follow the bar grid from the start position") {
    std::vector<float> samples = sine(1000, 0.5, 4);
    std::fill(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(2 * kRate), 0.0F);
    const auto analysis = analyzeAudio({samples}, {.sampleRate = kRate, .barSeconds = 2, .startSeconds = 2});
    REQUIRE(analysis.bars.size() == 2);
    CHECK(analysis.bars[0].bar == 2);
    CHECK(std::isinf(analysis.bars[0].rmsDb));
    CHECK(analysis.bars[1].bar == 3);
    CHECK(analysis.bars[1].rmsDb == Approx(-9.03).margin(0.05));
}

TEST_CASE("silence of half a second or more is a region and shorter gaps are not") {
    std::vector<float> samples = sine(1000, 0.5, 3);
    std::fill(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(kRate), 0.0F);
    std::fill(samples.begin() + static_cast<std::ptrdiff_t>(2 * kRate),
              samples.begin() + static_cast<std::ptrdiff_t>(2.2 * kRate), 0.0F);
    const auto analysis = analyzeAudio({samples}, {.sampleRate = kRate});
    REQUIRE(analysis.silence.size() == 1);
    CHECK(analysis.silence[0].start == Approx(0));
    CHECK(analysis.silence[0].end == Approx(1).margin(0.001));
}

TEST_CASE("samples at or above 0.999 count as clipped") {
    const std::vector<float> samples{0.5F, 0.999F, -1.0F, 0.998F, 1.2F};
    CHECK(analyzeAudio({samples, samples}, {.sampleRate = kRate}).clippedSamples == 6);
}

TEST_CASE("identical channels correlate at 1 and inverted channels at -1") {
    const std::vector<float> left = sine(440, 0.5, 1);
    std::vector<float> inverted = left;
    std::ranges::transform(inverted, inverted.begin(), [](float sample) { return -sample; });
    const auto same = analyzeAudio({left, left}, {.sampleRate = kRate});
    REQUIRE(same.stereo);
    CHECK(same.stereo->correlation == Approx(1));
    CHECK(same.stereo->balanceDb == Approx(0).margin(1e-9));
    CHECK(analyzeAudio({left, inverted}, {.sampleRate = kRate}).stereo->correlation == Approx(-1));
    CHECK_FALSE(analyzeAudio({left}, {.sampleRate = kRate}).stereo);
}

TEST_CASE("band energy is highest in the band that holds the tone") {
    const std::array<double, kFrequencyBands.size()> tones{40, 120, 350, 1000, 3000, 7000, 14000};
    for (std::size_t band = 0; band < tones.size(); ++band) {
        const auto analysis = analyzeAudio({sine(tones[band], 0.5, 1)}, {.sampleRate = kRate});
        CAPTURE(kFrequencyBands[band].name);
        CHECK(std::ranges::max_element(analysis.bandDb) - analysis.bandDb.begin() == static_cast<std::ptrdiff_t>(band));
    }
}

TEST_CASE("tone balance puts a 1 kHz sine in the middle band at its level, with its centroid at 1 kHz") {
    const auto tone = analyzeTone({sine(1000, 0.5, 2), sine(1000, 0.5, 2)}, kRate);
    CHECK(tone.activeRmsDb == Approx(20 * std::log10(0.5 / std::numbers::sqrt2)).margin(0.05));
    CHECK(tone.bandShare[2] > 0.99);
    CHECK(tone.centroidHz == Approx(1000).epsilon(0.02));

    const auto low = analyzeTone({sine(60, 0.5, 2)}, kRate);
    CHECK(low.bandShare[0] > 0.99);
    const auto high = analyzeTone({sine(9000, 0.5, 2)}, kRate);
    CHECK(high.bandShare[4] > 0.99);
}

TEST_CASE("active RMS skips silence, and silence reads minus infinity with no band shares") {
    std::vector<float> mostlySilent(static_cast<std::size_t>(8 * kRate));
    std::ranges::copy(sine(1000, 0.5, 2), mostlySilent.begin());
    CHECK(analyzeTone({mostlySilent}, kRate).activeRmsDb ==
          Approx(20 * std::log10(0.5 / std::numbers::sqrt2)).margin(0.05));

    const auto silence = analyzeTone({std::vector<float>(48000)}, kRate);
    CHECK(std::isinf(silence.activeRmsDb));
    CHECK(silence.bandShare == std::array<double, 5>{});
    CHECK(silence.centroidHz == 0);
}

TEST_CASE("a track meter fed in uneven chunks reads what the whole signal reads") {
    std::vector<float> left = sine(kRate / 4, 0.8, 2.3, std::numbers::pi / 4);
    const std::vector<float> right = sine(150, 0.3, 2.3);
    std::fill_n(left.begin() + 30000, 20000, 0.0F);
    const std::vector<std::vector<float>> whole{left, right};
    const auto analysis = analyzeAudio(whole, {.sampleRate = kRate});
    const auto tone = analyzeTone(whole, kRate);

    emerick::engine::TrackMeter meter(2, kRate);
    for (std::size_t begin = 0, size = 3; begin < left.size(); begin += size, size = (size * 7) % 9001 + 1) {
        const auto end = static_cast<std::ptrdiff_t>(std::min(left.size(), begin + size));
        const auto first = static_cast<std::ptrdiff_t>(begin);
        meter.add(std::vector<std::vector<float>>{{left.begin() + first, left.begin() + end},
                                                  {right.begin() + first, right.begin() + end}});
    }
    const auto levels = meter.levels();
    CHECK(levels.integratedLufs == Approx(analysis.levels.integratedLufs).margin(1e-9));
    CHECK(levels.samplePeakDbfs == analysis.levels.samplePeakDbfs);
    CHECK(levels.truePeakDbtp == analysis.levels.truePeakDbtp);
    CHECK(levels.tone.activeRmsDb == Approx(tone.activeRmsDb).margin(1e-9));
    CHECK(levels.tone.centroidHz == Approx(tone.centroidHz).margin(1e-6));
    for (std::size_t band = 0; band < tone.bandShare.size(); ++band) {
        CHECK(levels.tone.bandShare[band] == Approx(tone.bandShare[band]).margin(1e-9));
    }
}

TEST_CASE("a loudness meter fed in chunks reads the integrated loudness a track meter reads") {
    const std::vector<float> left = sine(440, 0.6, 3.1);
    const std::vector<float> right = sine(90, 0.2, 3.1);
    emerick::engine::TrackMeter track(2, kRate);
    emerick::engine::LoudnessMeter loudness(2, kRate);
    for (std::size_t begin = 0; begin < left.size(); begin += 7001) {
        const auto first = static_cast<std::ptrdiff_t>(begin);
        const auto end = static_cast<std::ptrdiff_t>(std::min(left.size(), begin + 7001));
        const std::vector<std::vector<float>> chunk{{left.begin() + first, left.begin() + end},
                                                    {right.begin() + first, right.begin() + end}};
        track.add(chunk);
        loudness.add(chunk);
    }
    CHECK(loudness.integratedLufs() == track.levels().integratedLufs);
}

TEST_CASE("matching loudness lifts a quiet mix to the target and keeps its true peak under the ceiling") {
    std::vector<float> quiet = sine(220, 0.02, 4);
    for (std::size_t frame = 0; frame < quiet.size(); frame += 12000) {
        quiet[frame] = 0.2F;
    }
    const std::vector<std::vector<float>> channels{quiet, quiet};
    const auto signal = replayInChunks(channels, 24000);
    const auto match = emerick::engine::matchLoudness(signal, 2, kRate, -14, -1);
    REQUIRE(match.gainDb);
    CHECK(match.integratedLufs == Approx(-14).margin(0.5));
    const auto limited = limitedSignal(signal, *match.gainDb, -1);
    CHECK(emerick::engine::integratedLufs(limited, kRate) == Approx(match.integratedLufs));
    CHECK(emerick::engine::truePeakDbtp(limited) <= -1.0);
    CHECK(limited.front().size() == quiet.size());
}

TEST_CASE("matching loudness in chunks gives what it gives on the whole signal") {
    std::vector<float> left = sine(220, 0.3, 3);
    std::vector<float> right = sine(330, 0.5, 3);
    for (std::size_t frame = 0; frame < left.size(); frame += 7000) {
        left[frame] = 0.9F;
    }
    const std::vector<std::vector<float>> channels{left, right};
    const auto whole = emerick::engine::matchLoudness(replayInChunks(channels, left.size()), 2, kRate, -9, -1);
    const auto chunked = emerick::engine::matchLoudness(replayInChunks(channels, 1000), 2, kRate, -9, -1);
    REQUIRE((whole.gainDb && chunked.gainDb));
    CHECK(*chunked.gainDb == Approx(*whole.gainDb).margin(1e-6));
    CHECK(chunked.integratedLufs == Approx(whole.integratedLufs).margin(1e-6));
    const auto wholeOut = limitedSignal(replayInChunks(channels, left.size()), *whole.gainDb, -1);
    const auto chunkedOut = limitedSignal(replayInChunks(channels, 1000), *chunked.gainDb, -1);
    REQUIRE(chunkedOut.front().size() == left.size());
    for (std::size_t channel = 0; channel < 2; ++channel) {
        for (std::size_t frame = 0; frame < left.size(); ++frame) {
            REQUIRE(chunkedOut[channel][frame] == Approx(wholeOut[channel][frame]).margin(1e-6));
        }
    }
}

TEST_CASE("matching loudness leaves digital silence alone") {
    const auto match =
        emerick::engine::matchLoudness(replayInChunks({std::vector<float>(48000)}, 24000), 1, kRate, -14, -1);
    CHECK(!match.gainDb);
    CHECK(std::isinf(match.integratedLufs));
}
