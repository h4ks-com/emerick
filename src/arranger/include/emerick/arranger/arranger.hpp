#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "emerick/arranger/style.hpp"
#include "emerick/core/time.hpp"
#include "emerick/yamaha/chord.hpp"
#include "emerick/yamaha/section.hpp"

namespace emerick::arranger {

struct MidiEvent {
    core::Ticks at = 0;
    std::array<std::uint8_t, 3> bytes{};
    bool operator==(const MidiEvent&) const = default;
};

// Plays a style's sections on the chords it is given, in host ticks at core::kTicksPerQuarter. Only the constructor
// allocates, so render can run in a realtime callback. Its bar grid counts from the tick start was given.
class Arranger {
public:
    static constexpr std::size_t kDefaultCapacity = 4096;

    explicit Arranger(const Style& style, std::size_t capacity = kDefaultCapacity);

    void setAutoFill(bool on) { autoFill_ = on; }
    void setChord(const yamaha::Chord& chord, core::Ticks at);
    // A fill or a break starts at the next beat and lands on its Main at the next bar line; a Main without a fill,
    // an Intro and an Ending start at the next bar line. While stopped, it picks the section start begins with.
    void request(yamaha::Section kind, int variation, core::Ticks at);
    void start(core::Ticks at);
    void stop(core::Ticks at);

    [[nodiscard]] bool playing() const { return current_ != nullptr; }
    [[nodiscard]] const StyleSection* currentSection() const { return current_; }

    // The events in [from, to), after what setChord, start and stop sent since the last render.
    std::span<const MidiEvent> render(core::Ticks from, core::Ticks to);

private:
    struct Held {
        bool on = false;
        std::uint8_t destination = 0;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
    };

    struct Switch {
        const StyleSection* section = nullptr;
        core::Ticks at = 0;
        core::Ticks start = 0;
    };

    [[nodiscard]] core::Ticks length(const StyleSection& section) const;
    [[nodiscard]] core::Ticks hostTicks(std::int64_t styleTicks) const;
    [[nodiscard]] core::Ticks gridAfter(core::Ticks at, core::Ticks unit) const;
    [[nodiscard]] core::Ticks gridBefore(core::Ticks at, core::Ticks unit) const;
    [[nodiscard]] core::Ticks boundaryAfter(core::Ticks at) const;
    [[nodiscard]] bool onMain() const;
    [[nodiscard]] const StyleSection* fallbackMain() const;

    void requestMain(const StyleSection* main, core::Ticks at);
    void startFill(const StyleSection* fill, const StyleSection* landing, core::Ticks at);
    void switchAtBar(const StyleSection* section, core::Ticks at);
    void enter(const StyleSection* section, core::Ticks start);
    void advance(core::Ticks at);
    void playRange(core::Ticks from, core::Ticks to);
    void play(const StyleEvent& event, core::Ticks at);
    void noteOn(int channel, const ChannelRule& rule, std::uint8_t note, std::uint8_t velocity, core::Ticks at);
    void noteOff(int channel, std::uint8_t note, core::Ticks at);
    void releaseAll(core::Ticks at, std::vector<MidiEvent>& into);
    static void push(std::vector<MidiEvent>& into, const MidiEvent& event);

    const Style* style_;
    core::Ticks bar_ = 0;
    core::Ticks beat_ = 0;
    bool autoFill_ = true;
    yamaha::Chord chord_;
    core::Ticks origin_ = 0;
    const StyleSection* first_ = nullptr;
    const StyleSection* current_ = nullptr;
    core::Ticks currentStart_ = 0;
    const StyleSection* playingMain_ = nullptr;
    const StyleSection* landing_ = nullptr;
    std::optional<Switch> next_;
    std::array<std::array<Held, 128>, kChannels> held_{};
    std::array<std::uint8_t, kChannels> bankMsb_{};
    std::vector<MidiEvent> pending_;
    std::vector<MidiEvent> out_;
};

}  // namespace emerick::arranger
