#include "emerick/yamaha/chord.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdlib>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>

namespace emerick::yamaha {

namespace {

constexpr std::string_view kLetters = "CDEFGAB";
constexpr std::array<int, 7> kSemitones{0, 2, 4, 5, 7, 9, 11};
constexpr int kNatural = 3;
constexpr int kMaxAccidentals = 3;

// The row number is the chord type byte of Yamaha Chord Control.
constexpr auto kChordTypes = std::to_array<ChordType>({
    {"", {0, 4, 7}},
    {"6", {0, 4, 7, 9}},
    {"maj7", {0, 4, 7, 11}},
    {"maj7(#11)", {0, 4, 7, 11, 18}},
    {"add9", {0, 4, 7, 14}},
    {"maj9", {0, 4, 7, 11, 14}},
    {"6/9", {0, 4, 7, 9, 14}},
    {"aug", {0, 4, 8}},
    {"m", {0, 3, 7}},
    {"m6", {0, 3, 7, 9}},
    {"m7", {0, 3, 7, 10}},
    {"m7b5", {0, 3, 6, 10}},
    {"m(add9)", {0, 3, 7, 14}},
    {"m9", {0, 3, 7, 10, 14}},
    {"m11", {0, 3, 7, 10, 17}},
    {"mMaj7", {0, 3, 7, 11}},
    {"mMaj9", {0, 3, 7, 11, 14}},
    {"dim", {0, 3, 6}},
    {"dim7", {0, 3, 6, 9}},
    {"7", {0, 4, 7, 10}},
    {"7sus4", {0, 5, 7, 10}},
    {"7b5", {0, 4, 6, 10}},
    {"9", {0, 4, 7, 10, 14}},
    {"7(#11)", {0, 4, 7, 10, 18}},
    {"13", {0, 4, 7, 10, 21}},
    {"7(b9)", {0, 4, 7, 10, 13}},
    {"7(b13)", {0, 4, 7, 10, 20}},
    {"7(#9)", {0, 4, 7, 10, 15}},
    {"maj7aug", {0, 4, 8, 11}},
    {"7aug", {0, 4, 8, 10}},
    {"(1+8)", {0, 12}},
    {"5", {0, 7}},
    {"sus4", {0, 5, 7}},
    {"sus2", {0, 2, 7}},
    {"N.C.", {}},
});
static_assert(kChordTypes[kNoChordType].name == "N.C.");

std::string accidentalText(int accidentals) {
    std::string text(static_cast<std::size_t>(std::abs(accidentals)), accidentals < 0 ? 'b' : '#');
    return text;
}

std::optional<int> typeFromSuffix(std::string_view suffix) {
    const auto index =
        std::ranges::distance(kChordTypes.begin(), std::ranges::find(kChordTypes, suffix, &ChordType::name));
    if (std::cmp_equal(index, kChordTypes.size())) {
        return std::nullopt;
    }
    return static_cast<int>(index);
}

std::size_t noteLength(std::string_view text) {
    if (text.empty() || !kLetters.contains(text.front())) {
        return 0;
    }
    std::size_t length = 1;
    while (length < text.size() && text[length] == text[1] && (text[1] == '#' || text[1] == 'b')) {
        ++length;
    }
    return length;
}

std::optional<Note> noteFromName(std::string_view name) {
    const auto code = noteCode(name);
    return code ? noteFromCode(*code) : std::nullopt;
}

constexpr int kPitchClasses = 12;
constexpr std::array<std::string_view, kPitchClasses> kSpelling{"C",  "Db", "D",  "Eb", "E",  "F",
                                                                "F#", "G",  "Ab", "A",  "Bb", "B"};

using PitchSet = std::bitset<kPitchClasses>;

PitchSet above(const PitchSet& pitches, int root) {
    PitchSet result;
    for (int pitch = 0; pitch < kPitchClasses; ++pitch) {
        result[static_cast<std::size_t>((pitch - root + kPitchClasses) % kPitchClasses)] =
            pitches[static_cast<std::size_t>(pitch)];
    }
    return result;
}

PitchSet pitchesOf(const ChordType& type) {
    PitchSet result;
    for (const int interval : type.intervals()) {
        result.set(static_cast<std::size_t>(interval % kPitchClasses));
    }
    return result;
}

std::optional<int> typeSpelling(const PitchSet& pitches, int root) {
    if (!pitches[static_cast<std::size_t>(root)]) {
        return std::nullopt;
    }
    const PitchSet intervals = above(pitches, root);
    const auto types = std::span(kChordTypes).first(kNoChordType);
    const auto found = std::ranges::find_if(types, [&](const ChordType& type) { return pitchesOf(type) == intervals; });
    return found == types.end() ? std::nullopt : std::optional{static_cast<int>(found - types.begin())};
}

Note spelled(int pitchClass) { return {pitchClass, std::string(kSpelling[static_cast<std::size_t>(pitchClass)])}; }

std::optional<Chord> firstSpelling(const PitchSet& pitches, int bass, bool overBass) {
    for (int offset = 0; offset < kPitchClasses; ++offset) {
        const int root = (bass + offset) % kPitchClasses;
        if (const auto type = typeSpelling(pitches, root)) {
            const bool slash = overBass || root != bass;
            return Chord{spelled(root), *type, slash ? std::optional{spelled(bass)} : std::nullopt};
        }
    }
    return std::nullopt;
}

}  // namespace

bool isChordType(int type) { return type >= 0 && std::cmp_less(type, kChordTypes.size()); }

const ChordType& chordType(int type) { return kChordTypes.at(static_cast<std::size_t>(type)); }

std::span<const ChordType> chordTypes() { return kChordTypes; }

std::optional<Note> noteFromCode(std::uint8_t code) {
    const int step = (code & 0x0F) - 1;
    const int accidentals = (code >> 4) - kNatural;
    if (step < 0 || step > 6 || accidentals < -kMaxAccidentals || accidentals > kMaxAccidentals) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(step);
    return Note{(kSemitones[index] + accidentals + 12) % 12, kLetters[index] + accidentalText(accidentals)};
}

std::optional<std::uint8_t> noteCode(std::string_view name) {
    if (name.empty() || noteLength(name) != name.size()) {
        return std::nullopt;
    }
    const auto step = kLetters.find(name.front());
    const int count = static_cast<int>(name.size()) - 1;
    if (count > kMaxAccidentals) {
        return std::nullopt;
    }
    const int accidentals = name.size() > 1 && name[1] == 'b' ? -count : count;
    return static_cast<std::uint8_t>(((accidentals + kNatural) << 4) | static_cast<int>(step + 1));
}

std::string Chord::name() const {
    if (isNoChord()) {
        return std::string(chordType(kNoChordType).name);
    }
    std::string text = root.name + std::string(chordType(type).name);
    if (bass) {
        text += "/" + bass->name;
    }
    return text;
}

std::optional<Chord> Chord::parse(std::string_view text) {
    Chord chord;
    if (text == chordType(kNoChordType).name) {
        chord.type = kNoChordType;
        return chord;
    }
    std::optional<Note> bass;
    if (const auto slash = text.rfind('/'); slash != std::string_view::npos) {
        bass = noteFromName(text.substr(slash + 1));
        if (bass) {
            text = text.substr(0, slash);
        }
    }
    const auto rootLength = noteLength(text);
    const auto root = noteFromName(text.substr(0, rootLength));
    const auto type = typeFromSuffix(text.substr(rootLength));
    if (!root || !type || *type == kNoChordType) {
        return std::nullopt;
    }
    chord.root = *root;
    chord.type = *type;
    if (bass != root) {
        chord.bass = bass;
    }
    return chord;
}

std::vector<int> Chord::notes(int baseOctaveNote) const {
    std::vector<int> result;
    if (isNoChord()) {
        return result;
    }
    if (bass) {
        result.push_back(baseOctaveNote - 12 + bass->pitchClass);
    }
    for (const int interval : chordType(type).intervals()) {
        result.push_back(baseOctaveNote + root.pitchClass + interval);
    }
    return result;
}

std::optional<Chord> recognizeChord(std::span<const int> notes) {
    if (notes.empty()) {
        return std::nullopt;
    }
    PitchSet pitches;
    for (const int note : notes) {
        pitches.set(static_cast<std::size_t>(note % kPitchClasses));
    }
    const int bass = std::ranges::min(notes) % kPitchClasses;
    if (auto chord = firstSpelling(pitches, bass, false)) {
        return chord;
    }
    PitchSet upper = pitches;
    upper.reset(static_cast<std::size_t>(bass));
    return upper.any() ? firstSpelling(upper, bass, true) : std::nullopt;
}

}  // namespace emerick::yamaha
