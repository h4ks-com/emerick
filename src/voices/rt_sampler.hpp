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

// A state variable low-pass filter for both sides of a note, or a one-pole that uses only a1 and ic1.
struct Filter {
    bool onePole = false;
    double a1 = 0.0;
    double a2 = 0.0;
    double a3 = 0.0;
    std::array<double, 2> ic1{};
    std::array<double, 2> ic2{};
};

inline constexpr std::size_t kPolyphony = 64;
inline constexpr std::size_t kChannels = 16;

// Plays the zones of a bank as notes come in. Every member function runs on the audio thread and never allocates.
class Sampler {
public:
    void noteOn(const Bank& bank, int key, int velocity, int channel, const PlaySettings& settings);
    void noteOff(int key, int channel);
    void setSustain(int channel, bool down);
    void releaseAll();
    void silence();
    // Adds the playing notes to both channels.
    void render(std::span<float> left, std::span<float> right);

private:
    enum class Stage : std::uint8_t { idle, attack, decay1, decay2, sustain, release };

    struct Note {
        Stage stage = Stage::idle;
        const BankZone* zone = nullptr;
        int key = 0;
        int channel = 0;
        bool sustained = false;
        std::uint64_t started = 0;
        double outputRate = 0.0;
        double position = 0.0;
        double step = 0.0;
        float gainLeft = 0.0F;
        float gainRight = 0.0F;
        Envelope envelope;
        double level = 0.0;
        double attackStep = 0.0;
        double decay1Ratio = 1.0;
        double decay2Ratio = 1.0;
        double releaseRatio = 1.0;
        Filter filter;
        double cutoffHertz = 0.0;
        double cutoffCents = 0.0;
        std::size_t cutoffSegment = 0;
        double cutoffHold = 0.0;
        double lfoPhase = 0.0;
        double lfoStep = 0.0;
        double lfoDelay = 0.0;
    };

    Note& freeNote();
    void chokeGroup(int group, int key, int channel);
    static void release(Note& note);
    static bool advanceEnvelope(Note& note);
    static bool advanceCutoff(Note& note, const CutoffEnvelope& envelope);
    static void renderNote(Note& note, std::span<float> left, std::span<float> right);

    std::array<Note, kPolyphony> notes_{};
    std::array<bool, kChannels> sustain_{};
    std::uint64_t clock_ = 0;
};

}  // namespace emerick::voices
