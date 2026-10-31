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
    // The zone whose root key, tune and sample rate pitch the waves: this zone, or the clean zone playing for a noisy
    // one.
    const Zone* pitch = nullptr;
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
    // Starts the key-off zones of the bank, when one is given, at the velocity the key went down with.
    void noteOff(const Bank* bank, int key, int channel, const PlaySettings& settings);
    void setSustain(int channel, bool down);
    // Sets the channel's level and its pan from -1 (left) to 1 (right), which sounding notes glide to.
    void setMix(int channel, float level, float pan);
    void setBend(int channel, double semitones);
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
        float gain = 0.0F;
        float mixLeft = 0.0F;
        float mixRight = 0.0F;
        float targetLeft = 0.0F;
        float targetRight = 0.0F;
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

    struct Channel {
        bool sustain = false;
        float level = 1.0F;
        float pan = 0.0F;
        double bendRatio = 1.0;
    };

    Note& freeNote();
    void startZone(const BankZone& zone, int key, int velocity, int channel, const PlaySettings& settings);
    void chokeGroup(int group, int key, int channel);
    void aim(Note& note) const;
    static void release(Note& note);
    static bool advanceEnvelope(Note& note);
    static bool advanceCutoff(Note& note, const CutoffEnvelope& envelope);
    static void renderNote(Note& note, double bendRatio, std::span<float> left, std::span<float> right);

    std::array<Note, kPolyphony> notes_{};
    std::array<Channel, kChannels> channels_{};
    std::array<std::array<std::uint8_t, 128>, kChannels> velocities_{};
    std::uint64_t clock_ = 0;
};

}  // namespace emerick::voices
