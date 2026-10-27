#include "emerick/yamaha/sysex.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace emerick::yamaha;

namespace {

std::vector<std::uint8_t> section(std::uint8_t code, std::uint8_t on) {
    return {0xF0, 0x43, 0x7E, 0x00, code, on, 0xF7};
}

}  // namespace

TEST_CASE("a button press decodes to the section the Style track pressed it for") {
    CHECK(decodeButtonPress(12, std::nullopt) == SectionPress{Section::Main, 1, true});
    CHECK(decodeButtonPress(22, std::nullopt) == SectionPress{Section::Intro, 1, true});
    CHECK(decodeButtonPress(31, std::nullopt) == SectionPress{Section::Ending, 0, true});
    CHECK(decodeButtonPress(8, std::nullopt) == SectionPress{Section::Break, 0, true});
    CHECK(decodeButtonPress(6, 2) == SectionPress{Section::Fill, 0, true});
    CHECK(decodeButtonPress(9, 1) == SectionPress{Section::FillTo, 2, true});
    CHECK(decodeButtonPress(5, 1) == SectionPress{Section::FillTo, 0, true});
    CHECK_FALSE(decodeButtonPress(5, 0).has_value());
    CHECK_FALSE(decodeButtonPress(9, std::nullopt).has_value());
    CHECK_FALSE(decodeButtonPress(0, 0).has_value());
    CHECK_FALSE(decodeButtonPress(7, 0).has_value());
}

TEST_CASE("chord control decodes root, type and bass") {
    const std::vector<std::uint8_t> dm7{0xF0, 0x43, 0x7E, 0x02, 0x32, 0x0A, 0x7F, 0x7F, 0xF7};
    CHECK(decodeChordControl(dm7)->name() == "Dm7");
    const std::vector<std::uint8_t> cOverE{0xF0, 0x43, 0x7E, 0x02, 0x31, 0x00, 0x33, 0x7F, 0xF7};
    CHECK(decodeChordControl(cOverE)->name() == "C/E");
    const std::vector<std::uint8_t> noChord{0xF0, 0x43, 0x7E, 0x02, 0x31, 0x22, 0x7F, 0x7F, 0xF7};
    CHECK(decodeChordControl(noChord)->name() == "N.C.");
    const std::vector<std::uint8_t> sameBass{0xF0, 0x43, 0x7E, 0x02, 0x31, 0x00, 0x31, 0x7F, 0xF7};
    CHECK(decodeChordControl(sameBass)->name() == "C");
}

TEST_CASE("chord control rejects malformed messages") {
    CHECK_FALSE(decodeChordControl(std::vector<std::uint8_t>{0xF0, 0x43, 0x7E, 0x02, 0x31, 0x23, 0x7F, 0x7F, 0xF7}));
    CHECK_FALSE(decodeChordControl(std::vector<std::uint8_t>{0xF0, 0x43, 0x7E, 0x02, 0x38, 0x00, 0x7F, 0x7F, 0xF7}));
    CHECK_FALSE(decodeChordControl(std::vector<std::uint8_t>{0xF0, 0x43, 0x7E, 0x02, 0x31, 0x00, 0x7F, 0xF7}));
    CHECK_FALSE(decodeChordControl(section(0x08, 0x7F)));
}

TEST_CASE("section control decodes every section and the on flag") {
    CHECK(decodeSectionControl(section(0x00, 0x7F)) == SectionPress{Section::Intro, 0, true});
    CHECK(decodeSectionControl(section(0x0A, 0x7F)) == SectionPress{Section::Main, 2, true});
    CHECK(decodeSectionControl(section(0x0B, 0x00)) == SectionPress{Section::Main, 3, false});
    CHECK(decodeSectionControl(section(0x11, 0x7F)) == SectionPress{Section::FillTo, 1, true});
    CHECK(decodeSectionControl(section(0x18, 0x7F)) == SectionPress{Section::Break, 0, true});
    CHECK(decodeSectionControl(section(0x22, 0x7F)) == SectionPress{Section::Ending, 2, true});
    CHECK_FALSE(decodeSectionControl(section(0x03, 0x7F)));
    CHECK_FALSE(decodeSectionControl(section(0x0C, 0x7F)));
    CHECK_FALSE(decodeSectionControl(section(0x30, 0x7F)));
}
