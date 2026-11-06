#include <algorithm>
#include <cmath>
#include <ranges>
#include <span>
#include <vector>

#include "emerick/engine/audio_analysis.hpp"
#include "rt_effects.hpp"
#include "rt_limiter.hpp"

namespace emerick::engine {

namespace {

constexpr int kPasses = 4;
constexpr double kCloseEnoughLu = 0.05;

using Channels = std::vector<std::vector<float>>;

double integratedLufsOf(const SignalReplay& signal, std::size_t channels, double sampleRate) {
    LoudnessMeter meter(channels, sampleRate);
    signal([&meter](std::span<const std::vector<float>> chunk) { meter.add(chunk); });
    return meter.integratedLufs();
}

}  // namespace

void limitSignal(const SignalReplay& signal, std::size_t channels, double sampleRate, double gainDb, double ceilingDbtp,
                 const ChunkSink& sink) {
    const EffectSpec& spec = effectSpec(EffectType::limiter);
    EffectParams params = defaultParams(spec);
    params.at(static_cast<std::size_t>(LimiterParam::ceiling)) = static_cast<float>(ceilingDbtp);
    Limiter limiter{channels, sampleRate};
    limiter.update(params, {.sampleRate = sampleRate});
    const auto gain = static_cast<float>(std::pow(10.0, gainDb / 20.0));
    std::size_t latencyLeft = limiter.latency();
    const auto play = [&](Channels block) {
        const std::vector<float*> pointers =
            block | std::views::transform([](std::vector<float>& channel) { return channel.data(); }) |
            std::ranges::to<std::vector>();
        const std::size_t frames = block.front().size();
        limiter.process(pointers, 0, static_cast<core::Frames>(frames));
        const std::size_t late = std::min(latencyLeft, frames);
        latencyLeft -= late;
        if (late == frames) {
            return;
        }
        for (std::vector<float>& channel : block) {
            channel.erase(channel.begin(), channel.begin() + static_cast<std::ptrdiff_t>(late));
        }
        sink(block);
    };
    signal([&](std::span<const std::vector<float>> chunk) {
        play(chunk | std::views::transform([gain](const std::vector<float>& channel) {
                 return channel | std::views::transform([gain](float sample) { return sample * gain; }) |
                        std::ranges::to<std::vector>();
             }) |
             std::ranges::to<std::vector>());
    });
    play(Channels(channels, std::vector<float>(limiter.latency())));
}

LoudnessMatch matchLoudness(const SignalReplay& signal, std::size_t channels, double sampleRate, double targetLufs,
                            double ceilingDbtp) {
    const double measured = integratedLufsOf(signal, channels, sampleRate);
    if (!std::isfinite(measured)) {
        return {.gainDb = std::nullopt, .integratedLufs = measured};
    }
    double gainDb = targetLufs - measured;
    double played = gainDb;
    double reached = measured;
    for (int pass = 0; pass == 0 || (pass < kPasses && std::abs(targetLufs - reached) > kCloseEnoughLu); ++pass) {
        played = gainDb;
        reached = integratedLufsOf(
            [&](const ChunkSink& sink) { limitSignal(signal, channels, sampleRate, played, ceilingDbtp, sink); },
            channels, sampleRate);
        gainDb += targetLufs - reached;
    }
    return {.gainDb = played, .integratedLufs = reached};
}

}  // namespace emerick::engine
