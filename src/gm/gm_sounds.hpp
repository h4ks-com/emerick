#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace emerick::gm {

inline constexpr std::uint32_t kSoundParam = 0;
inline constexpr std::uint32_t kDrumKitParam = 1;
inline constexpr std::uint32_t kFollowParam = 2;
inline constexpr std::uint32_t kHighestNoteParam = 3;
inline constexpr int kNotes = 128;

// The state key of an optional drum map: the General MIDI note each incoming note plays, or -1 to drop it.
inline constexpr std::string_view kDrumMapKey = "drumMap";
using DrumMap = std::array<int, kNotes>;

// Yamaha and Roland modules select a drum kit on any channel with these bank select MSBs.
constexpr bool isDrumBank(int msb) { return msb == 120 || msb == 126 || msb == 127; }

// The 128 General MIDI Level 1 sounds, in program order.
inline constexpr std::array<std::string_view, 128> kSounds{
    "Acoustic Grand Piano",
    "Bright Acoustic Piano",
    "Electric Grand Piano",
    "Honky-tonk Piano",
    "Electric Piano 1",
    "Electric Piano 2",
    "Harpsichord",
    "Clavi",
    "Celesta",
    "Glockenspiel",
    "Music Box",
    "Vibraphone",
    "Marimba",
    "Xylophone",
    "Tubular Bells",
    "Dulcimer",
    "Drawbar Organ",
    "Percussive Organ",
    "Rock Organ",
    "Church Organ",
    "Reed Organ",
    "Accordion",
    "Harmonica",
    "Tango Accordion",
    "Acoustic Guitar (nylon)",
    "Acoustic Guitar (steel)",
    "Electric Guitar (jazz)",
    "Electric Guitar (clean)",
    "Electric Guitar (muted)",
    "Overdriven Guitar",
    "Distortion Guitar",
    "Guitar Harmonics",
    "Acoustic Bass",
    "Electric Bass (finger)",
    "Electric Bass (pick)",
    "Fretless Bass",
    "Slap Bass 1",
    "Slap Bass 2",
    "Synth Bass 1",
    "Synth Bass 2",
    "Violin",
    "Viola",
    "Cello",
    "Contrabass",
    "Tremolo Strings",
    "Pizzicato Strings",
    "Orchestral Harp",
    "Timpani",
    "String Ensemble 1",
    "String Ensemble 2",
    "Synth Strings 1",
    "Synth Strings 2",
    "Choir Aahs",
    "Voice Oohs",
    "Synth Voice",
    "Orchestra Hit",
    "Trumpet",
    "Trombone",
    "Tuba",
    "Muted Trumpet",
    "French Horn",
    "Brass Section",
    "Synth Brass 1",
    "Synth Brass 2",
    "Soprano Sax",
    "Alto Sax",
    "Tenor Sax",
    "Baritone Sax",
    "Oboe",
    "English Horn",
    "Bassoon",
    "Clarinet",
    "Piccolo",
    "Flute",
    "Recorder",
    "Pan Flute",
    "Blown Bottle",
    "Shakuhachi",
    "Whistle",
    "Ocarina",
    "Lead 1 (square)",
    "Lead 2 (sawtooth)",
    "Lead 3 (calliope)",
    "Lead 4 (chiff)",
    "Lead 5 (charang)",
    "Lead 6 (voice)",
    "Lead 7 (fifths)",
    "Lead 8 (bass + lead)",
    "Pad 1 (new age)",
    "Pad 2 (warm)",
    "Pad 3 (polysynth)",
    "Pad 4 (choir)",
    "Pad 5 (bowed)",
    "Pad 6 (metallic)",
    "Pad 7 (halo)",
    "Pad 8 (sweep)",
    "FX 1 (rain)",
    "FX 2 (soundtrack)",
    "FX 3 (crystal)",
    "FX 4 (atmosphere)",
    "FX 5 (brightness)",
    "FX 6 (goblins)",
    "FX 7 (echoes)",
    "FX 8 (sci-fi)",
    "Sitar",
    "Banjo",
    "Shamisen",
    "Koto",
    "Kalimba",
    "Bag pipe",
    "Fiddle",
    "Shanai",
    "Tinkle Bell",
    "Agogo",
    "Steel Drums",
    "Woodblock",
    "Taiko Drum",
    "Melodic Tom",
    "Synth Drum",
    "Reverse Cymbal",
    "Guitar Fret Noise",
    "Breath Noise",
    "Seashore",
    "Bird Tweet",
    "Telephone Ring",
    "Helicopter",
    "Applause",
    "Gunshot",
};

struct DrumKit {
    std::string_view name;
    int program = 0;
};

// The GS drum kits that GeneralUser GS holds in bank 128.
inline constexpr std::array<DrumKit, 9> kDrumKits{{{"Standard", 0},
                                                   {"Room", 8},
                                                   {"Power", 16},
                                                   {"Electronic", 24},
                                                   {"TR-808", 25},
                                                   {"Jazz", 32},
                                                   {"Brush", 40},
                                                   {"Orchestra", 48},
                                                   {"SFX", 56}}};

// The kit General MIDI plays for a drum program, or nullptr when it holds no such kit.
constexpr const DrumKit* drumKitOf(int program) {
    const auto* const kit = std::ranges::find(kDrumKits, program, &DrumKit::program);
    return kit == kDrumKits.end() ? nullptr : kit;
}

// A range of 1-based General MIDI programs and the one that stands for the whole family.
struct Family {
    int first = 1;
    int last = 1;
    int typical = 1;
};

// The General MIDI families by the category names a voice list gives its voices.
struct Category {
    std::string_view name;
    Family family;
};

inline constexpr std::array<Category, 14> kCategories{{
    {"Piano", {1, 8, 1}},
    {"E.Piano", {5, 6, 5}},
    {"Percussion", {9, 16, 12}},
    {"Organ", {17, 21, 17}},
    {"Accordion", {22, 24, 22}},
    {"A.Guitar", {25, 26, 26}},
    {"E.Guitar", {27, 32, 28}},
    {"Bass", {33, 40, 34}},
    {"Strings", {41, 52, 49}},
    {"Choir", {53, 55, 53}},
    {"Brass", {57, 64, 62}},
    {"Woodwind", {65, 80, 66}},
    {"Synth", {81, 88, 81}},
    {"Pad", {89, 96, 90}},
}};

}  // namespace emerick::gm
