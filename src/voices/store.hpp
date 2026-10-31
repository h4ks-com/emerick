#pragma once

#include <array>
#include <compare>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace emerick::voices {

// The bank select and program change that pick a voice on the keyboard.
struct VoiceAddress {
    int msb = 0;
    int lsb = 0;
    int program = 0;
    auto operator<=>(const VoiceAddress&) const = default;
};

// An element's amplitude envelope: a linear attack to attackGain, two decays and a release to silence, each
// falling evenly in decibels over its seconds.
struct Envelope {
    double attackSeconds = 0.0;
    double decay1Seconds = 0.0;
    double decay2Seconds = 0.0;
    double releaseSeconds = 0.0;
    double attackGain = 1.0;
    double decay1Gain = 1.0;
    double sustainGain = 1.0;
};

// One value per MIDI velocity or key.
using Curve = std::array<double, 128>;

struct CutoffSegment {
    double cents = 0.0;
    double centsPerSecond = 0.0;
};

// A cutoff envelope in cents: it holds its initial offset, moves through the first four segments, stays at the fourth
// and moves through the last after note off.
struct CutoffEnvelope {
    double initialCents = 0.0;
    double holdSeconds = 0.0;
    std::array<CutoffSegment, 5> segments{};
};

// A two-pole low-pass, or a 6 dB per octave one-pole, whose cutoff the note's velocity, its key and the envelope move
// in cents.
struct LowPass {
    double hertz = 0.0;
    double q = 0.0;
    bool onePole = false;
    std::optional<Curve> velocityCents;
    std::optional<Curve> keyCents;
    std::optional<CutoffEnvelope> envelope;
};

// A triangle LFO that starts after its delay and moves pitch, cutoff and level by up to its depths.
struct Lfo {
    double hertz = 0.0;
    double delaySeconds = 0.0;
    double pitchCents = 0.0;
    double cutoffCents = 0.0;
    double amplitudeDb = 0.0;
};

// One sample a voice plays for a range of notes and velocities, flattened from its element and key bank.
struct Zone {
    int keyLo = 0;
    int keyHi = 127;
    int velocityLo = 0;
    int velocityHi = 127;
    // The element plays the notes this many semitones higher.
    int keyShift = 0;
    int rootKey = 60;
    double tuneCents = 0.0;
    bool fixedPitch = false;
    bool oneShot = false;
    // A drum key that ignores note off plays to its end.
    bool receivesNoteOff = true;
    // A note of a drum key in a group other than 0 cuts the other keys of its group, as a closed hi-hat cuts an open
    // one.
    int alternateGroup = 0;
    // A key-off zone sounds once when its key is released, as the clunk of a clavinet or the damper of a Rhodes.
    bool keyOff = false;
    double sampleRate = 44100.0;
    float gain = 1.0F;
    // Empty when the element names no touch curve, which scales the level by the velocity.
    std::optional<Curve> velocityDb;
    float pan = 0.0F;
    std::string leftWave;
    // Empty for a mono zone, which plays leftWave on both sides.
    std::string rightWave;
    // Empty when the element names no envelope, which plays the instrument's Attack and Release.
    std::optional<Envelope> envelope;
    std::optional<LowPass> lowPass;
    Lfo lfo;
};

struct Voice {
    std::string pack;
    std::string name;
    VoiceAddress address;
    std::vector<Zone> zones;
};

// The voices of an Expansion Manager store that we can play, sorted by pack and name, and where each wave lives.
struct Store {
    std::vector<Voice> voices;
    std::unordered_map<std::string, std::filesystem::path> waves;
};

// The folder kFolderVariable names, else the folder Yamaha Expansion Manager keeps its packs in on this computer, or
// empty where it has none.
std::filesystem::path defaultStoreFolder();

// Reads every pack in the folder, leaving out the zones whose waves we cannot decode; a missing or empty folder is an
// empty store.
Store loadStore(const std::filesystem::path& folder);

// A Yamaha note name such as C-2, F#3 or G8, where C-2 is MIDI note 0.
std::optional<int> yamahaNote(std::string_view name);
std::string yamahaNoteName(int note);

}  // namespace emerick::voices
