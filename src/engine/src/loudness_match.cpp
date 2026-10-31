#include <cmath>
#include <ranges>
#include <vector>

#include "emerick/engine/audio_analysis.hpp"
#include "rt_effects.hpp"
#include "rt_limiter.hpp"

namespace emerick::engine {

namespace {

constexpr int kPasses = 4;
constexpr double kCloseEnoughLu = 0.05;

using Channels = std::vector<std::vector<float>>;

// Plays the channels through a limiter and drops its latency, so the output lines up with the input.
void limit(Channels& channels, double sampleRate, double ceilingDbtp) {
    const EffectSpec& spec = effectSpec(EffectType::limiter);
    EffectParams params = defaultParams(spec);
    params.at(static_cast<std::size_t>(LimiterParam::ceiling)) = static_cast<float>(ceilingDbtp);
    Limiter limiter{channels.size(), sampleRate};
    limiter.update(params, {.sampleRate = sampleRate});
    const auto latency = static_cast<std::ptrdiff_t>(limiter.latency());
    for (std::vector<float>& channel : channels) {
        channel.resize(channel.size() + limiter.latency());
    }
    const std::vector<float*> pointers =
        channels | std::views::transform([](std::vector<float>& channel) { return channel.data(); }) |
        std::ranges::to<std::vector>();
    limiter.process(pointers, 0, std::ssize(channels.front()));
    for (std::vector<float>& channel : channels) {
        channel.erase(channel.begin(), channel.begin() + latency);
    }
}

}  // namespace

double matchLoudness(Channels& channels, double sampleRate, double targetLufs, double ceilingDbtp) {
    const double measured = integratedLufs(channels, sampleRate);
    if (!std::isfinite(measured)) {
        return measured;
    }
    const Channels original = channels;
    double gainDb = targetLufs - measured;
    double reached = measured;
    for (int pass = 0; pass == 0 || (pass < kPasses && std::abs(targetLufs - reached) > kCloseEnoughLu); ++pass) {
        const auto gain = static_cast<float>(std::pow(10.0, gainDb / 20.0));
        channels = original | std::views::transform([&](const std::vector<float>& channel) {
                       return channel | std::views::transform([&](float sample) { return sample * gain; }) |
                              std::ranges::to<std::vector>();
                   }) |
                   std::ranges::to<std::vector>();
        limit(channels, sampleRate, ceilingDbtp);
        reached = integratedLufs(channels, sampleRate);
        gainDb += targetLufs - reached;
    }
    return reached;
}

}  // namespace emerick::engine
