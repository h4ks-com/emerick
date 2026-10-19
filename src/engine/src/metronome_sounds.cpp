#include "metronome_sounds.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

#include "snd_file.hpp"

namespace emerick::engine {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr float kPeak = 0.5F;
constexpr float kHatPeak = 0.3F;
constexpr double kMaxOneShotSeconds = 4.0;

std::vector<float> render(double seconds, double sampleRate, auto sample) {
    std::vector<float> samples(static_cast<std::size_t>(seconds * sampleRate));
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] = static_cast<float>(sample(static_cast<double>(index) / sampleRate));
    }
    return samples;
}

std::vector<float> normalized(std::vector<float> samples, float peak) {
    const float loudest = std::ranges::max(samples, {}, [](float sample) { return std::abs(sample); });
    if (loudest > 0.0F) {
        std::ranges::transform(samples, samples.begin(),
                               [&](float sample) { return sample * peak / std::abs(loudest); });
    }
    return samples;
}

// A sine with a fast decay rings like a struck wood block; the soft attack keeps it from clicking.
std::vector<float> blip(double frequency, double sampleRate) {
    return normalized(render(0.06, sampleRate,
                             [&](double t) {
                                 const double attack = std::min(1.0, t / 0.0005);
                                 const double body = std::sin(kTwoPi * frequency * t) * std::exp(-t / 0.012);
                                 const double overtone =
                                     0.25 * std::sin(kTwoPi * 2.01 * frequency * t) * std::exp(-t / 0.004);
                                 return attack * (body + overtone);
                             }),
                      kPeak);
}

std::vector<float> kick(double sampleRate) {
    double phase = 0.0;
    return normalized(render(0.35, sampleRate,
                             [&](double t) {
                                 phase += kTwoPi * (48.0 + 110.0 * std::exp(-t / 0.035)) / sampleRate;
                                 return std::sin(phase) * std::exp(-t / 0.14);
                             }),
                      kPeak);
}

// A one-pole high-pass on white noise takes the rumble out, so the noise reads as snare wires or a hat.
// We want the same drums on every run, so a fixed xorshift sequence makes the noise.
auto highPassedNoise(double cutoff, double sampleRate) {
    return [noise = std::uint32_t{0x9E3779B9}, pole = std::exp(-kTwoPi * cutoff / sampleRate), last = 0.0,
            out = 0.0]() mutable {
        noise ^= noise << 13U;
        noise ^= noise >> 17U;
        noise ^= noise << 5U;
        const double in =
            static_cast<double>(noise) / static_cast<double>(std::numeric_limits<std::uint32_t>::max()) * 2.0 - 1.0;
        out = pole * (out + in - last);
        last = in;
        return out;
    };
}

std::vector<float> snare(double sampleRate) {
    auto noise = highPassedNoise(1200.0, sampleRate);
    return normalized(render(0.2, sampleRate,
                             [&](double t) {
                                 const double tone = 0.5 * std::sin(kTwoPi * 185.0 * t) * std::exp(-t / 0.04);
                                 return tone + noise() * std::exp(-t / 0.06);
                             }),
                      kPeak);
}

std::vector<float> hat(double sampleRate) {
    auto noise = highPassedNoise(7000.0, sampleRate);
    return normalized(render(0.08, sampleRate, [&](double t) { return noise() * std::exp(-t / 0.018); }), kHatPeak);
}

}  // namespace

DrumSounds synthesizeDrums(double sampleRate) {
    return {{blip(1568.0, sampleRate), blip(1046.5, sampleRate), kick(sampleRate), snare(sampleRate), hat(sampleRate)}};
}

std::expected<std::vector<float>, std::string> loadOneShot(const std::filesystem::path& path, double sampleRate) {
    SF_INFO info{};
    const SndFile file{sf_open(path.c_str(), SFM_READ, &info)};
    if (!file) {
        return std::unexpected("cannot open metronome sample " + path.string());
    }
    const auto frames = std::min<sf_count_t>(info.frames, std::llround(kMaxOneShotSeconds * info.samplerate));
    std::vector<float> interleaved(static_cast<std::size_t>(frames * info.channels));
    const sf_count_t read = sf_readf_float(file.get(), interleaved.data(), frames);
    const auto channels = static_cast<std::size_t>(info.channels);
    std::vector<float> mono(static_cast<std::size_t>(std::max<sf_count_t>(read, 0)));
    for (std::size_t frame = 0; frame < mono.size(); ++frame) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            mono[frame] += interleaved[frame * channels + channel] / static_cast<float>(channels);
        }
    }
    const double step = static_cast<double>(info.samplerate) / sampleRate;
    if (step == 1.0 || mono.empty()) {
        return mono;
    }
    std::vector<float> resampled(static_cast<std::size_t>(static_cast<double>(mono.size()) / step));
    for (std::size_t index = 0; index < resampled.size(); ++index) {
        const double at = static_cast<double>(index) * step;
        const auto left = std::min(static_cast<std::size_t>(at), mono.size() - 1);
        const auto right = std::min(left + 1, mono.size() - 1);
        const auto fraction = static_cast<float>(at - static_cast<double>(left));
        resampled[index] = mono[left] + (mono[right] - mono[left]) * fraction;
    }
    return resampled;
}

}  // namespace emerick::engine
