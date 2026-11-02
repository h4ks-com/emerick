#include "emerick/engine/window_meter.hpp"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <random>
#include <ranges>
#include <vector>

#include "emerick/engine/mix_issues.hpp"

using Catch::Approx;
using namespace emerick::engine;

namespace {

constexpr double kRate = 48000;
constexpr std::size_t kSecond = 48000;

std::vector<float> sine(double hz, double amplitude, std::size_t frames) {
    return std::views::iota(std::size_t{0}, frames) | std::views::transform([&](std::size_t frame) {
               return static_cast<float>(amplitude *
                                         std::sin(2 * std::numbers::pi * hz * static_cast<double>(frame) / kRate));
           }) |
           std::ranges::to<std::vector>();
}

std::vector<float> noise(double amplitude, std::size_t frames) {
    std::mt19937 random(7);
    std::uniform_real_distribution<double> spread(-amplitude, amplitude);
    return std::views::iota(std::size_t{0}, frames) |
           std::views::transform([&](std::size_t) { return static_cast<float>(spread(random)); }) |
           std::ranges::to<std::vector>();
}

// A Hann shaped 20 ms blip of hz every interval frames.
std::vector<float> blips(double hz, std::size_t interval, std::size_t frames) {
    constexpr std::size_t kBlip = 960;
    std::vector<float> samples = sine(hz, 0.5, frames);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t phase = frame % interval;
        const double envelope =
            phase < kBlip ? 0.5 * (1 - std::cos(2 * std::numbers::pi * static_cast<double>(phase) / kBlip)) : 0;
        samples[frame] *= static_cast<float>(envelope);
    }
    return samples;
}

// A 60 ms beep of hz every interval frames, with 5 ms ramps.
std::vector<float> beeps(double hz, double amplitude, std::size_t interval, std::size_t frames) {
    constexpr std::size_t kBeep = 2880;
    constexpr std::size_t kRamp = 240;
    std::vector<float> samples = sine(hz, amplitude, frames);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t phase = frame % interval;
        const std::size_t edge = std::min(phase, kBeep - std::min(phase, kBeep));
        const double envelope = phase >= kBeep ? 0
                                : edge < kRamp
                                    ? 0.5 * (1 - std::cos(std::numbers::pi * static_cast<double>(edge) / kRamp))
                                    : 1;
        samples[frame] *= static_cast<float>(envelope);
    }
    return samples;
}

std::vector<WindowLevels> measure(const std::vector<float>& samples, std::size_t window) {
    std::vector<std::size_t> ends;
    for (std::size_t end = window; end <= samples.size(); end += window) {
        ends.push_back(end);
    }
    WindowMeter meter(2, kRate, ends);
    const std::size_t chunk = 7000;
    for (std::size_t begin = 0; begin < samples.size(); begin += chunk) {
        const std::vector<float> part(
            samples.begin() + static_cast<std::ptrdiff_t>(begin),
            samples.begin() + static_cast<std::ptrdiff_t>(std::min(begin + chunk, samples.size())));
        meter.add(std::vector{part, part});
    }
    return meter.windows();
}

InspectedOutput output(const std::vector<float>& samples, std::size_t window, OutputRole role = OutputRole::track) {
    TrackMeter meter(2, kRate);
    meter.add(std::vector{samples, samples});
    return {.role = role, .levels = meter.levels(), .windows = measure(samples, window)};
}

std::vector<IssueKind> kinds(const std::vector<MixIssue>& issues) {
    return issues | std::views::transform(&MixIssue::kind) | std::ranges::to<std::vector>();
}

}  // namespace

TEST_CASE("a window meter reads a sine as a tone and noise as no tone") {
    const std::vector<WindowLevels> tone = measure(sine(3000, 0.5, 4 * kSecond), kSecond);
    REQUIRE(tone.size() == 4);
    CHECK(tone[1].tonalPeakHz == Approx(3000).epsilon(0.005));
    CHECK(tone[1].tonality > 0.95);
    CHECK(tone[1].peakDb == Approx(-6.02).margin(0.1));
    CHECK(tone[1].activeRmsDb == Approx(-9.03).margin(0.1));
    CHECK(tone[1].peakDb - tone[1].rmsDb == Approx(3.01).margin(0.1));
    CHECK(tone[1].bandShare[3] > 0.95);
    CHECK(tone[1].onsets <= 1);
    CHECK(tone[1].clipped == 0);

    const std::vector<WindowLevels> hiss = measure(noise(0.3, 2 * kSecond), kSecond);
    CHECK(hiss[0].tonality < 0.1);

    const std::vector<WindowLevels> low = measure(sine(200, 0.5, kSecond), kSecond);
    CHECK(low[0].tonalPeakHz == 0);
    CHECK(low[0].tonality == 0);
}

TEST_CASE("a window meter counts each pitched blip, clipped samples and a cut short last window") {
    const std::vector<WindowLevels> hits = measure(blips(3000, kSecond / 4, 2 * kSecond), kSecond);
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].onsets == 4);
    CHECK(hits[0].hitSeconds == Approx(0.02).margin(0.006));
    CHECK(hits[0].peakDb - hits[0].rmsDb > 10);
    CHECK(hits[0].tonality > 0.4);
    CHECK(measure(beeps(3000, 0.5, kSecond / 2, kSecond), kSecond)[0].hitSeconds == Approx(0.06).margin(0.006));

    std::vector<float> loud = sine(100, 1.2, kSecond + (kSecond / 2));
    std::vector<WindowLevels> clipped = measure(loud, kSecond);
    CHECK(clipped.size() == 1);
    CHECK(clipped[0].clipped > 0);
    WindowMeter meter(1, kRate, {kSecond, 2 * kSecond});
    meter.add(std::vector<std::vector<float>>{loud});
    CHECK(meter.windows().size() == 2);
}

TEST_CASE("inspecting a mix finds a beep that holds its tone, and no beep in short pitched hits") {
    const std::size_t frames = 8 * kSecond;
    const std::vector<InspectedOutput> outputs{output(blips(3000, kSecond / 4, frames), kSecond),
                                               output(beeps(3000, 0.5, kSecond / 2, frames), kSecond)};
    const std::vector<MixIssue> issues = inspectMix(outputs, std::vector<double>(8, 2.0));
    const auto beep = std::ranges::find(issues, IssueKind::beep, &MixIssue::kind);
    REQUIRE(beep != issues.end());
    CHECK(beep->output == 1);
    CHECK(std::ranges::count(issues, IssueKind::beep, &MixIssue::kind) == 1);
}

TEST_CASE("inspecting a mix finds a whine, a beep, clipping, a DC offset, silence and an imbalance") {
    const std::size_t frames = 8 * kSecond;
    std::vector<float> offset = noise(0.05, frames);
    for (float& sample : offset) {
        sample += 0.05F;
    }
    std::vector<float> clipping = noise(0.05, frames);
    clipping[3 * kSecond] = 1.0F;
    const std::vector<InspectedOutput> outputs{output(sine(6000, 0.1, frames), kSecond),
                                               output(beeps(3000, 0.1, kSecond / 2, frames), kSecond),
                                               output(clipping, kSecond, OutputRole::mix),
                                               output(offset, kSecond),
                                               output(std::vector<float>(frames), kSecond, OutputRole::bus),
                                               output(noise(0.9, frames), kSecond)};
    const std::vector<double> beats(8, 2.0);
    const std::vector<MixIssue> issues = inspectMix(outputs, beats);
    INFO(issues.size());

    const auto issue = [&](IssueKind kind) {
        const auto found = std::ranges::find(issues, kind, &MixIssue::kind);
        REQUIRE(found != issues.end());
        return *found;
    };
    const MixIssue whine = issue(IssueKind::whine);
    CHECK(whine.output == 0);
    CHECK(whine.firstWindow == 0);
    CHECK(whine.lastWindow == 7);
    const MixIssue beep = issue(IssueKind::beep);
    CHECK(beep.output == 1);
    CHECK(beep.evidence[0].value == Approx(3000).epsilon(0.01));
    CHECK(beep.evidence[1].value == Approx(1).margin(0.1));
    const MixIssue clipped = issue(IssueKind::clipping);
    CHECK(clipped.output == 2);
    CHECK(clipped.firstWindow == 3);
    CHECK(clipped.lastWindow == 3);
    CHECK(clipped.severity == IssueSeverity::high);
    CHECK(issue(IssueKind::dc).output == 3);
    CHECK(issue(IssueKind::silent).output == 4);
    CHECK(issue(IssueKind::imbalance).output == 5);
    CHECK(issues.front().severity == IssueSeverity::high);
    CHECK_FALSE(std::ranges::contains(kinds(issues), IssueKind::harsh));
}
