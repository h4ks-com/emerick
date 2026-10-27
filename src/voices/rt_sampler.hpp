#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "store.hpp"
#include "wave.hpp"

namespace emerick::voices {

struct BankZone {
    const Zone* zone = nullptr;
    const Wave* left = nullptr;
    const Wave* right = nullptr;
};

// A voice with its waves decoded, which the main thread builds and the audio thread plays.
struct Bank {
    std::vector<BankZone> zones;
};

struct PlaySettings {
    double outputRate = 48000.0;
    float volume = 1.0F;
    double tuneSemitones = 0.0;
    double attackSeconds = 0.0;
    double releaseSeconds = 0.0;
};

inline constexpr std::size_t kPolyphony = 64;
inline constexpr std::size_t kChannels = 16;

// Plays the zones of a bank as notes come in. Every member function runs on the audio thread and never allocates.
class Sampler {
public:
    void noteOn(const Bank& bank, int key, int velocity, int channel, const PlaySettings& settings);
    void noteOff(int key, int channel, const PlaySettings& settings);
    void setSustain(int channel, bool down, const PlaySettings& settings);
    void releaseAll(const PlaySettings& settings);
    void silence();
    // Adds the playing notes to both channels.
    void render(std::span<float> left, std::span<float> right);

private:
    enum class Stage : std::uint8_t { idle, attack, hold, release };

    struct Note {
        Stage stage = Stage::idle;
        const BankZone* zone = nullptr;
        int key = 0;
        int channel = 0;
        bool sustained = false;
        std::uint64_t started = 0;
        double position = 0.0;
        double step = 0.0;
        float gainLeft = 0.0F;
        float gainRight = 0.0F;
        float level = 0.0F;
        float attackStep = 0.0F;
        float releaseStep = 0.0F;
    };

    Note& freeNote();
    static void release(Note& note, const PlaySettings& settings);
    static void renderNote(Note& note, std::span<float> left, std::span<float> right);

    std::array<Note, kPolyphony> notes_{};
    std::array<bool, kChannels> sustain_{};
    std::uint64_t clock_ = 0;
};

}  // namespace emerick::voices
