#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "emerick/engine/plugin.hpp"
#include "style_builder.hpp"

using namespace emerick::engine;
using namespace emerick::arranger::test;
using Json = nlohmann::json;

namespace {

constexpr double kRate = 48000.0;
constexpr emerick::core::Frames kBlock = 480;
// At 120 BPM a beat lasts 24000 frames, so a block covers 0.02 beats.
constexpr double kBeatsPerBlock = 0.02;
constexpr std::uint32_t kAutoFill = 0;

struct Played {
    double beat = 0.0;
    std::vector<std::uint8_t> bytes;
    emerick::core::Frames offset = 0;
};

Bytes text(const std::string& value) { return {value.begin(), value.end()}; }

Bytes chordSysex(std::uint8_t root, std::uint8_t type) {
    return {0xF0, 0x43, 0x7E, 0x02, root, type, 0x7F, 0x7F, 0xF7};
}

Bytes button(std::uint8_t cc) { return {0xBF, cc, 127}; }

constexpr std::uint8_t kC = 0x31;
constexpr std::uint8_t kG = 0x35;
constexpr std::uint8_t kMajor = 0;
constexpr std::uint8_t kMainA = 11;
constexpr std::uint8_t kMainB = 12;

struct Rig {
    std::filesystem::path folder =
        std::filesystem::temp_directory_path() / ("emerick-arranger-" + std::to_string(std::random_device{}()));
    std::filesystem::path style = folder / "Demo.sty";
    std::shared_ptr<Plugin> instrument;
    std::array<std::vector<float>, 2> channels{std::vector<float>(kBlock), std::vector<float>(kBlock)};
    std::array<float*, 2> mix{channels[0].data(), channels[1].data()};

    Rig() {
        std::filesystem::create_directories(folder);
        const Bytes bytes = demoStyle();
        std::ofstream(style, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        instrument = create(Json{{"style", style.string()}});
    }

    ~Rig() { std::filesystem::remove_all(folder); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;

    static std::shared_ptr<Plugin> create(const Json& state) {
        auto created = createClapPlugin(EMERICK_ARRANGER_CLAP, "org.emerick.arranger");
        REQUIRE(created.has_value());
        REQUIRE((*created)->loadState(text(state.dump())));
        REQUIRE((*created)->activate(kRate, kBlock));
        return *created;
    }

    // Plays one block from beat with messages at frame offsets and returns what the device sent, by beat.
    std::vector<Played> block(InstrumentTransport transport,
                              const std::vector<std::pair<emerick::core::Frames, Bytes>>& messages = {}) {
        for (const auto& [frame, bytes] : messages) {
            instrument->queueMidi(bytes, static_cast<std::int64_t>(static_cast<double>(frame) / kRate * 1e9));
        }
        transport.bpm = 120.0;
        instrument->processBlock(mix, kBlock, 0, transport);
        std::vector<Played> played;
        for (const InstrumentMidi& message : instrument->producedMidi()) {
            played.push_back({transport.beats + (static_cast<double>(message.offset) / kRate * 2.0),
                              {message.bytes.begin(), message.bytes.end()},
                              message.offset});
        }
        return played;
    }

    std::vector<Played> play(double from, double to, std::optional<std::pair<double, double>> loop = std::nullopt) {
        std::vector<Played> played;
        for (double beat = from; beat < to - 1e-9; beat += kBeatsPerBlock) {
            const auto events = block({.playing = true,
                                       .beats = beat,
                                       .looping = loop.has_value(),
                                       .loopStartBeats = loop ? loop->first : 0.0,
                                       .loopEndBeats = loop ? loop->second : 0.0});
            played.insert(played.end(), events.begin(), events.end());
        }
        return played;
    }
};

std::vector<Played> notesOn(const std::vector<Played>& played, int channel) {
    std::vector<Played> notes;
    std::ranges::copy_if(played, std::back_inserter(notes), [&](const Played& message) {
        return message.bytes.size() == 3 && message.bytes[0] == (0x90 | channel) && message.bytes[2] > 0;
    });
    return notes;
}

bool hasNote(const std::vector<Played>& played, int channel, int pitch) {
    return std::ranges::any_of(notesOn(played, channel), [&](const Played& note) { return note.bytes[1] == pitch; });
}

}  // namespace

TEST_CASE("the device plays nothing until the transport plays, then the section and chord it was sent") {
    Rig rig;
    const auto stopped = rig.block({.playing = false}, {{0, chordSysex(kC, kMajor)}, {0, button(kMainA)}});
    CHECK(notesOn(stopped, 8).empty());
    CHECK(std::ranges::any_of(stopped, [](const Played& message) { return message.bytes[0] == 0xC8; }));

    const auto played = rig.play(0.0, 4.0);
    const auto drums = notesOn(played, 8);
    REQUIRE(drums.size() == 4);
    CHECK(drums.front().beat == 0.0);
    CHECK(drums.front().bytes[1] == kMainADrum);
    CHECK(hasNote(played, 10, 36));
}

TEST_CASE("the device follows a chord change and a Main the Style track presses a beat early") {
    Rig rig;
    rig.block({.playing = false}, {{0, chordSysex(kC, kMajor)}, {0, button(kMainA)}});
    rig.play(0.0, 7.0);
    std::vector<Played> played =
        rig.block({.playing = true, .beats = 7.0}, {{0, chordSysex(kG, kMajor)}, {0, button(kMainB)}});
    const auto rest = rig.play(7.0 + kBeatsPerBlock, 12.0);
    played.insert(played.end(), rest.begin(), rest.end());
    CHECK_FALSE(hasNote(played, 10, 36));
    const auto drums = notesOn(played, 8);
    REQUIRE_FALSE(drums.empty());
    CHECK(drums.back().bytes[1] == kMainBDrum);
}

TEST_CASE("a locate starts the device over from the section sent with it, at the bar line") {
    Rig rig;
    rig.block({.playing = false}, {{0, chordSysex(kC, kMajor)}, {0, button(kMainA)}});
    rig.play(0.0, 2.0);
    auto played = rig.block({.playing = true, .beats = 16.0}, {{0, button(kMainB)}});
    const auto rest = rig.play(16.0 + kBeatsPerBlock, 18.0);
    played.insert(played.end(), rest.begin(), rest.end());
    const auto drums = notesOn(played, 8);
    REQUIRE(drums.size() == 2);
    CHECK(drums.front().beat == 16.0);
    CHECK(drums.front().bytes[1] == kMainBDrum);
}

TEST_CASE("a cycle jump starts the device over at the cycle start with the section sent at the jump") {
    Rig rig;
    rig.block({.playing = false}, {{0, chordSysex(kC, kMajor)}, {0, button(kMainB)}});
    // The cycle ends 240 frames into the block that starts at beat 7.99.
    rig.play(4.0, 7.99, std::pair{4.0, 8.0});
    const auto jumped =
        rig.block({.playing = true, .beats = 7.99, .looping = true, .loopStartBeats = 4.0, .loopEndBeats = 8.0},
                  {{240, button(kMainA)}});
    const auto drums = notesOn(jumped, 8);
    REQUIRE(drums.size() == 1);
    CHECK(drums.front().bytes[1] == kMainADrum);
    CHECK(drums.front().offset == 240);
}

TEST_CASE("stopping the transport ends every note the device started") {
    Rig rig;
    rig.block({.playing = false}, {{0, chordSysex(kC, kMajor)}, {0, button(kMainA)}});
    const auto played = rig.play(0.0, 0.1);
    const auto stopped = rig.block({.playing = false, .beats = 0.1});
    const auto started = notesOn(played, 10).size() + notesOn(played, 11).size();
    REQUIRE(started > 0);
    const auto offs = std::ranges::count_if(stopped, [](const Played& message) {
        return (message.bytes[0] & 0xF0) == 0x80 || ((message.bytes[0] & 0xF0) == 0x90 && message.bytes[2] == 0);
    });
    CHECK(offs >= static_cast<std::ptrdiff_t>(started));
}

TEST_CASE("the device state keeps the style path, its hash and Auto fill") {
    Rig rig;
    rig.instrument->setParam(kAutoFill, 0.0);
    const auto saved = rig.instrument->saveState();
    REQUIRE(saved.has_value());
    const Json state = Json::parse(*saved);
    CHECK(state.at("style") == rig.style.string());
    CHECK(state.at("hash").get<std::string>().size() == 16);
    CHECK(state.at("autoFill") == false);

    const auto restored = Rig::create(state);
    CHECK(restored->paramValue(kAutoFill) == 0.0);
    CHECK(Json::parse(*restored->saveState()) == state);

    std::filesystem::remove(rig.style);
    const auto moved = Rig::create(state);
    CHECK(Json::parse(*moved->saveState()).at("style") == rig.style.string());
}
