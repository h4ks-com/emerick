#include "emerick/arranger/transpose.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <ranges>

#include "emerick/arranger/style.hpp"
#include "emerick/yamaha/chord.hpp"

namespace emerick::arranger {

namespace {

constexpr int kOctave = 12;
constexpr int kMaxNote = 127;
constexpr int kFlatThird = 3;
constexpr int kThird = 4;
constexpr int kFlatFifth = 6;
constexpr int kFifth = 7;
constexpr int kSharpFifth = 8;
constexpr int kSixth = 9;
constexpr int kFlatSeventh = 10;
constexpr int kSeventh = 11;

using Scale = std::array<int, 7>;
constexpr Scale kIonian{0, 2, 4, 5, 7, 9, 11};
constexpr Scale kLydian{0, 2, 4, 6, 7, 9, 11};
constexpr Scale kAeolian{0, 2, 3, 5, 7, 8, 10};
constexpr Scale kDorian{0, 2, 3, 5, 7, 9, 10};
constexpr Scale kMixolydian{0, 2, 4, 5, 7, 9, 10};

// The seven degrees a melody keeps over each chord type, in Chord Control order. A suspended chord's third degree
// is its fourth or second, so a source third follows it there.
constexpr auto kChordScales = std::to_array<Scale>({
    kIonian,                 // major
    kIonian,                 // 6
    kIonian,                 // maj7
    kLydian,                 // maj7(#11)
    kIonian,                 // add9
    kIonian,                 // maj9
    kIonian,                 // 6/9
    {0, 2, 4, 5, 8, 9, 11},  // aug
    kAeolian,                // m
    kDorian,                 // m6
    kDorian,                 // m7
    {0, 1, 3, 5, 6, 8, 10},  // m7b5
    kAeolian,                // m(add9)
    kDorian,                 // m9
    kDorian,                 // m11
    {0, 2, 3, 5, 7, 9, 11},  // mMaj7
    {0, 2, 3, 5, 7, 9, 11},  // mMaj9
    {0, 2, 3, 5, 6, 8, 9},   // dim
    {0, 2, 3, 5, 6, 8, 9},   // dim7
    kMixolydian,             // 7
    {0, 2, 5, 5, 7, 9, 10},  // 7sus4
    {0, 2, 4, 5, 6, 9, 10},  // 7b5
    kMixolydian,             // 9
    {0, 2, 4, 6, 7, 9, 10},  // 7(#11)
    kMixolydian,             // 13
    {0, 1, 4, 5, 7, 9, 10},  // 7(b9)
    {0, 2, 4, 5, 7, 8, 10},  // 7(b13)
    {0, 3, 4, 5, 7, 8, 10},  // 7(#9)
    {0, 2, 4, 6, 8, 9, 11},  // maj7aug
    {0, 2, 4, 6, 8, 9, 10},  // 7aug
    kIonian,                 // 1+8
    kIonian,                 // 1+5
    {0, 2, 5, 5, 7, 9, 11},  // sus4
    {0, 2, 2, 5, 7, 9, 11},  // sus2
});
static_assert(kChordScales.size() == yamaha::kNoChordType);

int pitchClass(int note) { return ((note % kOctave) + kOctave) % kOctave; }

// Ties go down, so a note a tritone away from every choice keeps the lower one.
int nearestWithPitchClass(int note, int target) {
    const int up = pitchClass(target - note);
    return up < kOctave / 2 ? note + up : note + up - kOctave;
}

bool hasInterval(int type, int interval) {
    return std::ranges::any_of(yamaha::chordType(type).intervals(),
                               [&](int value) { return pitchClass(value) == interval; });
}

bool isMinor(int type) { return hasInterval(type, kFlatThird) && !hasInterval(type, kThird); }
bool isMajor(int type) { return hasInterval(type, kThird); }

const Scale& scaleOf(int type) { return kChordScales.at(static_cast<std::size_t>(type)); }

int melodyInterval(int sourceType, int targetType, int interval) {
    const Scale& source = scaleOf(sourceType);
    const auto degree = static_cast<std::size_t>(
        std::ranges::count_if(source | std::views::drop(1), [&](int value) { return value <= interval; }));
    return scaleOf(targetType).at(degree) + interval - source.at(degree);
}

int chordInterval(int sourceType, int targetType, int interval) {
    const int melody = melodyInterval(sourceType, targetType, interval);
    int best = melody;
    int bestDistance = kOctave;
    for (const int tone : yamaha::chordType(targetType).intervals()) {
        const int candidate = nearestWithPitchClass(melody, tone);
        const int distance = std::abs(candidate - melody);
        if (distance < bestDistance || (distance == bestDistance && candidate < best)) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

struct MinorChanges {
    bool sixth = false;
    bool seventh = false;
    bool fifth = false;
};

MinorChanges minorChanges(NoteTable table) {
    switch (table) {
        case NoteTable::MelodicMinor:
            return {};
        case NoteTable::MelodicMinorFifth:
            return {.fifth = true};
        case NoteTable::HarmonicMinor:
            return {.sixth = true};
        case NoteTable::HarmonicMinorFifth:
            return {.sixth = true, .fifth = true};
        case NoteTable::NaturalMinor:
            return {.sixth = true, .seventh = true};
        case NoteTable::NaturalMinorFifth:
            return {.sixth = true, .seventh = true, .fifth = true};
        case NoteTable::Dorian:
            return {.seventh = true};
        case NoteTable::DorianFifth:
            return {.seventh = true, .fifth = true};
        default:
            return {};
    }
}

// The minor tables move only the third (and the sixth, seventh or fifth they name) between major and minor chords.
int minorInterval(NoteTable table, int sourceType, int targetType, int interval) {
    const auto changes = minorChanges(table);
    const auto swap = [&](int from, int to) { return interval == from ? to : interval; };
    if (isMajor(sourceType) && isMinor(targetType)) {
        interval = swap(kThird, kFlatThird);
        interval = changes.sixth ? swap(kSixth, kSharpFifth) : interval;
        interval = changes.seventh ? swap(kSeventh, kFlatSeventh) : interval;
    } else if (isMinor(sourceType) && isMajor(targetType)) {
        interval = swap(kFlatThird, kThird);
        interval = changes.sixth ? swap(kSharpFifth, kSixth) : interval;
        interval = changes.seventh ? swap(kFlatSeventh, kSeventh) : interval;
    }
    if (changes.fifth && interval == kFifth && !hasInterval(targetType, kFifth)) {
        if (hasInterval(targetType, kFlatFifth)) {
            return kFlatFifth;
        }
        if (hasInterval(targetType, kSharpFifth)) {
            return kSharpFifth;
        }
    }
    return interval;
}

int mappedInterval(NoteTable table, int sourceType, int targetType, int interval) {
    switch (table) {
        case NoteTable::Bypass:
            return interval;
        case NoteTable::Melody:
            return melodyInterval(sourceType, targetType, interval);
        case NoteTable::Chord:
        case NoteTable::GuitarAllPurpose:
        case NoteTable::GuitarStroke:
        case NoteTable::GuitarArpeggio:
            return chordInterval(sourceType, targetType, interval);
        default:
            return minorInterval(table, sourceType, targetType, interval);
    }
}

// High key is the highest chord root a part transposes up to; a root above it plays an octave lower.
int rootBelowHighKey(int root, int highKey) { return root <= highKey ? root : root - kOctave; }

int withinLimits(int note, const Transposition& transposition) {
    while (note < transposition.noteLow) {
        note += kOctave;
    }
    while (note > transposition.noteHigh) {
        note -= kOctave;
    }
    return std::clamp(note, 0, kMaxNote);
}

int bassNote(int note, const Transposition& transposition, const yamaha::Chord& chord) {
    if (transposition.bassOn && chord.bass && pitchClass(note) == chord.root.pitchClass) {
        return nearestWithPitchClass(note, chord.bass->pitchClass);
    }
    return note;
}

}  // namespace

bool playsOn(const ChannelRule& rule, const yamaha::Chord& chord) {
    const bool typePlays = ((rule.playsOnType >> chord.type) & 1U) != 0;
    return typePlays && (chord.isNoChord() || ((rule.playsOnRoot >> chord.root.pitchClass) & 1U) != 0);
}

const Transposition& transpositionFor(const ChannelRule& rule, int sourceNote) {
    if (sourceNote < rule.middleLow) {
        return rule.ranges[0];
    }
    return sourceNote > rule.middleHigh ? rule.ranges[2] : rule.ranges[1];
}

int transposeNote(const ChannelRule& rule, int sourceNote, const yamaha::Chord& chord, bool megaVoice) {
    const Transposition& transposition = transpositionFor(rule, sourceNote);
    const bool untouched = transposition.rule != NoteRule::RootTranspose && transposition.table == NoteTable::Bypass;
    if ((megaVoice && sourceNote >= kMegaVoiceFxNote) || chord.isNoChord() || untouched) {
        return sourceNote;
    }
    const int interval = pitchClass(sourceNote - rule.sourceRoot);
    const int mapped = mappedInterval(transposition.table, rule.sourceType, chord.type, interval);
    int note = 0;
    if (transposition.rule == NoteRule::RootTranspose) {
        const int shift = rootBelowHighKey(chord.root.pitchClass, transposition.highKey) -
                          rootBelowHighKey(rule.sourceRoot, transposition.highKey);
        note = sourceNote - interval + shift + mapped;
    } else {
        note = nearestWithPitchClass(sourceNote, chord.root.pitchClass + mapped);
    }
    return withinLimits(bassNote(note, transposition, chord), transposition);
}

std::optional<int> retriggerNote(const ChannelRule& rule, int sourceNote, int heldNote, const yamaha::Chord& chord,
                                 bool megaVoice) {
    if (!playsOn(rule, chord)) {
        return std::nullopt;
    }
    const Transposition& transposition = transpositionFor(rule, sourceNote);
    switch (transposition.retrigger) {
        case RetriggerRule::Stop:
            return std::nullopt;
        case RetriggerRule::PitchShift:
        case RetriggerRule::Retrigger:
            return transposeNote(rule, sourceNote, chord, megaVoice);
        case RetriggerRule::PitchShiftToRoot:
        case RetriggerRule::RetriggerToRoot:
        case RetriggerRule::NoteGenerator: {
            if (chord.isNoChord()) {
                return heldNote;
            }
            const bool onBass = transposition.bassOn && chord.bass;
            const int root = onBass ? chord.bass->pitchClass : chord.root.pitchClass;
            return withinLimits(nearestWithPitchClass(heldNote, root), transposition);
        }
    }
    return std::nullopt;
}

}  // namespace emerick::arranger
