#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <ranges>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "emerick/engine/plugin.hpp"
#include "emerick/engine/plugin_scan.hpp"
#include "sampler_params.hpp"

using namespace emerick::engine;
using Json = nlohmann::json;

namespace {

constexpr double kRate = 48000.0;
constexpr emerick::core::Frames kBlock = 480;
using Message = std::array<std::uint8_t, 3>;

const std::filesystem::path kSine = std::filesystem::path{EMERICK_SAMPLER_SINE} / "sine.sfz";

std::vector<std::uint8_t> stateFor(const std::filesystem::path& path) {
    const std::string text = Json{{"path", path.string()}}.dump();
    return {text.begin(), text.end()};
}

// Runs the main thread until the plugin has swapped in the instrument its thread loaded.
void waitLoaded(Plugin& instrument) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (instrument.paramValue(emerick::sampler::kLoadingParam) != 0.0) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        instrument.idle();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::shared_ptr<Plugin> sampler(const std::vector<std::uint8_t>& state) {
    auto created = createClapPlugin(EMERICK_SAMPLER_CLAP, "org.emerick.sampler");
    REQUIRE(created.has_value());
    REQUIRE((*created)->loadState(state));
    REQUIRE((*created)->activate(kRate, kBlock));
    waitLoaded(**created);
    return *created;
}

std::vector<float> render(Plugin& instrument, const std::vector<Message>& messages, int blocks) {
    for (const Message& message : messages) {
        instrument.queueMidi(message, 0);
    }
    std::vector<float> rendered;
    for (int block = 0; block < blocks; ++block) {
        std::vector<float> left(kBlock);
        std::vector<float> right(kBlock);
        const std::array<float*, 2> mix{left.data(), right.data()};
        instrument.processBlock(mix, kBlock, 0, {});
        rendered.insert(rendered.end(), left.begin(), left.end());
    }
    return rendered;
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

TEST_CASE("the sampler bundle describes one instrument") {
    const auto described = describeClapBundle(EMERICK_SAMPLER_CLAP);
    REQUIRE(described.has_value());
    REQUIRE(described->size() == 1);
    CHECK(described->front().id == "org.emerick.sampler");
    CHECK(described->front().name == "Emerick Sampler");
}

TEST_CASE("the sampler plays an SFZ note at its pitch") {
    const auto instrument = sampler(stateFor(kSine));
    CHECK(instrument->paramText(emerick::sampler::kProblemParam, 0.0).empty());
    const std::vector<float> a4 = render(*instrument, {Message{0x90, 69, 100}}, 50);
    const std::span<const float> held = std::span(a4).subspan(kBlock * 10);
    CHECK(loudest(held) > 0.05F);
    CHECK(std::abs(cents(frequency(held), 440.0)) < 10.0);
    render(*instrument, {Message{0x80, 69, 0}}, 20);
    const std::vector<float> a5 = render(*instrument, {Message{0x90, 81, 100}}, 50);
    CHECK(std::abs(cents(frequency(std::span(a5).subspan(kBlock * 10)), 880.0)) < 10.0);
}

TEST_CASE("the sampler saves the SFZ path and plays it again from that state") {
    const auto first = sampler(stateFor(kSine));
    const auto saved = first->saveState();
    REQUIRE(saved.has_value());
    CHECK(Json::parse(*saved).at("path") == kSine.string());
    const auto second = sampler(*saved);
    CHECK(loudest(render(*second, {Message{0x90, 69, 100}}, 20)) > 0.05F);
}

TEST_CASE("a sampler whose SFZ file is gone stays silent and says why") {
    const std::filesystem::path gone = std::filesystem::path{EMERICK_SAMPLER_SINE} / "gone.sfz";
    const auto instrument = sampler(stateFor(gone));
    CHECK(loudest(render(*instrument, {Message{0x90, 69, 100}}, 20)) == 0.0F);
    const std::string problem = instrument->paramText(emerick::sampler::kProblemParam, 0.0);
    CHECK(problem.contains("gone.sfz"));
    const auto saved = instrument->saveState();
    REQUIRE(saved.has_value());
    CHECK(Json::parse(*saved).at("path") == gone.string());
}

TEST_CASE("an active sampler swaps in a new SFZ file after a restart") {
    const auto instrument = sampler(stateFor(std::filesystem::path{EMERICK_SAMPLER_SINE} / "gone.sfz"));
    REQUIRE(instrument->loadState(stateFor(kSine)));
    waitLoaded(*instrument);
    CHECK(instrument->paramText(emerick::sampler::kProblemParam, 0.0).empty());
    CHECK(loudest(render(*instrument, {Message{0x90, 69, 100}}, 20)) > 0.05F);
}
