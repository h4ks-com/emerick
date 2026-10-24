#include "emerick/yamaha/chord.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "emerick/yamaha/sysex.hpp"

using namespace emerick::yamaha;

TEST_CASE("every chord type round trips through encode, decode, name and parse") {
    for (int type = 0; type < kNoChordType; ++type) {
        for (const char* root : {"C", "Db", "F#", "Bb"}) {
            const auto chord = Chord::parse(std::string(root) + std::string(chordType(type).name));
            INFO(root << " type " << type);
            REQUIRE(chord);
            CHECK(chord->type == type);
            CHECK(decodeChordControl(encodeChordControl(*chord)) == chord);
            CHECK(Chord::parse(chord->name()) == chord);
        }
    }
}

TEST_CASE("no chord round trips") {
    const auto chord = Chord::parse("N.C.");
    REQUIRE(chord);
    CHECK(decodeChordControl(encodeChordControl(*chord)) == chord);
    CHECK(chord->name() == "N.C.");
}

TEST_CASE("slash chords keep their bass spelling") {
    const auto chord = Chord::parse("Bb/D");
    REQUIRE(chord);
    CHECK(chord->root.pitchClass == 10);
    CHECK(chord->bass == Note{2, "D"});
    CHECK(chord->name() == "Bb/D");
    CHECK(Chord::parse("C6/9/E")->name() == "C6/9/E");
    CHECK(Chord::parse("C6/9")->type == 6);
    CHECK(Chord::parse("C/C")->bass == std::nullopt);
}

TEST_CASE("parse names from charts") {
    CHECK(Chord::parse("Dm7")->name() == "Dm7");
    CHECK(Chord::parse("Bbmaj7")->root.pitchClass == 10);
    CHECK(Chord::parse("F#m7b5")->type == 0x0B);
    CHECK(Chord::parse("N.C.")->isNoChord());
    CHECK_FALSE(Chord::parse("H"));
    CHECK_FALSE(Chord::parse("Cfoo"));
    CHECK_FALSE(Chord::parse(""));
}

TEST_CASE("note codes cover the accidental and letter nibbles") {
    CHECK(noteCode("C") == 0x31);
    CHECK(noteCode("Db") == 0x22);
    CHECK(noteCode("F#") == 0x44);
    CHECK(noteCode("Ab") == 0x26);
    CHECK(noteFromCode(0x26)->pitchClass == 8);
    CHECK(noteFromCode(0x26)->name == "Ab");
    CHECK_FALSE(noteFromCode(0x38));
    CHECK_FALSE(noteFromCode(0x71));
}

TEST_CASE("notes are a block voicing with the bass an octave below") {
    CHECK(Chord::parse("Dm7")->notes(48) == std::vector<int>{50, 53, 57, 60});
    CHECK(Chord::parse("C/E")->notes(48) == std::vector<int>{40, 48, 52, 55});
    CHECK(Chord::parse("N.C.")->notes(48).empty());
}

TEST_CASE("held notes name the chord they spell, over the lowest note") {
    const auto named = [](const std::vector<int>& notes) {
        const auto chord = recognizeChord(notes);
        return chord ? chord->name() : std::string{"none"};
    };
    CHECK(named({60, 64, 67}) == "C");
    CHECK(named({58, 61, 65, 68}) == "Bbm7");
    CHECK(named({67, 71, 74, 77}) == "G7");
    CHECK(named({64, 67, 72}) == "C/E");
    CHECK(named({50, 60, 64, 67}) == "Cadd9/D");
    CHECK(named({49, 60, 64, 67}) == "C/Db");
    CHECK(named({60, 64, 67, 69}) == "C6");
    CHECK(named({57, 60, 64, 67}) == "Am7");
    CHECK(named({60, 62, 67}) == "Csus2");
    CHECK(named({60, 72}) == "C(1+8)");
    CHECK(named({60, 61, 62}) == "none");
    CHECK(named({}) == "none");
}
