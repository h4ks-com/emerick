#include "emerick/app/dexed.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <ranges>
#include <string_view>

namespace emerick::app {

namespace {

constexpr std::size_t kCartridgeSize = 4104;
constexpr std::size_t kHeaderSize = 6;
constexpr std::size_t kVoiceSize = 128;
constexpr std::size_t kNameOffset = 118;
constexpr std::size_t kNameSize = 10;
constexpr std::size_t kUnpackedVoiceSize = 155;
constexpr std::size_t kOperators = 6;
constexpr std::size_t kPackedOperatorSize = 17;
constexpr std::size_t kUnpackedOperatorSize = 21;
constexpr std::array<std::uint8_t, 4> kJuceXmlMagic{0x56, 0x43, 0x32, 0x21};
constexpr std::size_t kJuceHeaderSize = 8;
constexpr std::string_view kJuceBase64 = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";
constexpr std::string_view kSysexAttribute = " base64:sysex=\"";
constexpr std::string_view kProgramAttribute = " base64:program=\"";
constexpr std::string_view kCurrentProgramAttribute = " currentProgram=\"";
constexpr std::string_view kOpSwitchAttribute = " opSwitch=\"";

bool startsBulkDump(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= kCartridgeSize && bytes[0] == 0xF0 && bytes[1] == 0x43 && (bytes[2] & 0xF0) == 0 &&
           bytes[3] == 0x09 && bytes[4] == 0x20 && bytes[5] == 0x00 && bytes[kCartridgeSize - 1] == 0xF7;
}

std::span<const std::uint8_t> packedVoice(const Dx7Cartridge& cartridge, std::size_t program) {
    return std::span(cartridge.sysex).subspan(kHeaderSize + (program * kVoiceSize), kVoiceSize);
}

// Dexed maps the DX7's own characters to ASCII and blanks the ones it cannot show.
char nameCharacter(std::uint8_t byte) {
    const auto code = static_cast<std::uint8_t>(byte & 0x7F);
    switch (code) {
        case 92:
            return 'Y';
        case 126:
            return '>';
        case 127:
            return '<';
        default:
            return code < 32 ? ' ' : static_cast<char>(code);
    }
}

std::uint8_t clampedParam(std::uint8_t value, std::uint8_t max) {
    return value <= max ? value
                        : static_cast<std::uint8_t>(static_cast<float>(value) / 255.0F * static_cast<float>(max));
}

// Unpacks a 128-byte cartridge voice into the 155 bytes of a single voice, as Dexed's Cartridge::unpackProgram does.
std::array<std::uint8_t, kUnpackedVoiceSize> unpackedVoice(std::span<const std::uint8_t> bulk) {
    std::array<std::uint8_t, kUnpackedVoiceSize> voice{};
    for (std::size_t op = 0; op < kOperators; ++op) {
        const auto packed = bulk.subspan(op * kPackedOperatorSize, kPackedOperatorSize);
        const auto out = std::span(voice).subspan(op * kUnpackedOperatorSize, kUnpackedOperatorSize);
        std::ranges::copy(packed.first(11), out.begin());
        const std::uint8_t curves = packed[11] & 0x0F;
        out[11] = curves & 3;
        out[12] = (curves >> 2) & 3;
        const std::uint8_t detuneRate = packed[12] & 0x7F;
        out[13] = detuneRate & 7;
        const std::uint8_t sensitivity = packed[13] & 0x1F;
        out[14] = sensitivity & 3;
        out[15] = (sensitivity >> 2) & 7;
        out[16] = packed[14] & 0x7F;
        const std::uint8_t coarseMode = packed[15] & 0x3F;
        out[17] = coarseMode & 1;
        out[18] = (coarseMode >> 1) & 0x1F;
        out[19] = packed[16] & 0x7F;
        out[20] = detuneRate >> 3;
    }
    for (std::size_t i = 0; i < 8; ++i) {
        voice[126 + i] = clampedParam(bulk[102 + i] & 0x7F, 99);
    }
    voice[134] = clampedParam(bulk[110] & 0x1F, 31);
    const std::uint8_t feedback = bulk[111] & 0x0F;
    voice[135] = feedback & 7;
    voice[136] = feedback >> 3;
    for (std::size_t i = 112; i < 116; ++i) {
        voice[i + 25] = bulk[i] & 0x7F;
    }
    const std::uint8_t lfo = bulk[116] & 0x7F;
    voice[141] = lfo & 1;
    voice[142] = (lfo >> 1) & 7;
    voice[143] = lfo >> 4;
    for (std::size_t i = 117; i < kVoiceSize; ++i) {
        voice[i + 27] = bulk[i] & 0x7F;
    }
    return voice;
}

bool bitAt(std::span<const std::uint8_t> bytes, std::size_t bit) {
    return bit / 8 < bytes.size() && ((bytes[bit / 8] >> (bit % 8)) & 1) != 0;
}

// JUCE's MemoryBlock base64: the byte count, a dot, then six bits a character from the lowest bit up.
std::string juceBase64(std::span<const std::uint8_t> bytes) {
    std::string text = std::to_string(bytes.size()) + '.';
    for (std::size_t bit = 0; bit < bytes.size() * 8; bit += 6) {
        std::size_t value = 0;
        for (std::size_t offset = 0; offset < 6; ++offset) {
            value |= static_cast<std::size_t>(bitAt(bytes, bit + offset)) << offset;
        }
        text += kJuceBase64[value];
    }
    return text;
}

std::optional<std::vector<std::uint8_t>> fromJuceBase64(std::string_view text) {
    const std::size_t dot = text.find('.');
    const std::string_view count = text.substr(0, dot);
    std::size_t size = 0;
    if (dot == std::string_view::npos ||
        std::from_chars(count.data(), count.data() + count.size(), size).ec != std::errc{}) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(size);
    std::size_t bit = 0;
    for (const char character : text.substr(dot + 1)) {
        const std::size_t value = kJuceBase64.find(character);
        if (value == std::string_view::npos) {
            return std::nullopt;
        }
        for (std::size_t offset = 0; offset < 6 && (bit + offset) / 8 < size; ++offset) {
            if (((value >> offset) & 1) != 0) {
                bytes[(bit + offset) / 8] |= static_cast<std::uint8_t>(1U << ((bit + offset) % 8));
            }
        }
        bit += 6;
    }
    return bytes;
}

std::optional<std::string_view> attribute(std::string_view xml, std::string_view prefix) {
    const std::size_t start = xml.find(prefix);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t from = start + prefix.size();
    const std::size_t end = xml.find('"', from);
    return end == std::string_view::npos ? std::nullopt : std::optional(xml.substr(from, end - from));
}

void replaceAttribute(std::string& xml, std::string_view prefix, std::string_view value) {
    if (const auto old = attribute(xml, prefix)) {
        const auto from = static_cast<std::size_t>(old->data() - xml.data());
        xml.replace(from, old->size(), value);
    }
}

}  // namespace

std::optional<Dx7Cartridge> parseDx7Cartridge(std::span<const std::uint8_t> bytes) {
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        if (startsBulkDump(bytes.subspan(at))) {
            const auto dump = bytes.subspan(at, kCartridgeSize);
            return Dx7Cartridge{.sysex = {dump.begin(), dump.end()}};
        }
    }
    return std::nullopt;
}

std::vector<std::string> dx7ProgramNames(const Dx7Cartridge& cartridge) {
    return std::views::iota(std::size_t{0}, kDx7Programs) | std::views::transform([&](std::size_t program) {
               std::string name = packedVoice(cartridge, program).subspan(kNameOffset, kNameSize) |
                                  std::views::transform(nameCharacter) | std::ranges::to<std::string>();
               name.erase(name.find_last_not_of(' ') + 1);
               return name;
           }) |
           std::ranges::to<std::vector>();
}

std::optional<DexedState> parseDexedState(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kJuceHeaderSize || !std::ranges::equal(bytes.first(4), kJuceXmlMagic)) {
        return std::nullopt;
    }
    const std::size_t length = std::size_t{bytes[4]} | (std::size_t{bytes[5]} << 8) | (std::size_t{bytes[6]} << 16) |
                               (std::size_t{bytes[7]} << 24);
    if (length > bytes.size() - kJuceHeaderSize) {
        return std::nullopt;
    }
    const auto text = bytes.subspan(kJuceHeaderSize, length);
    DexedState state{.xml = {text.begin(), text.end()}, .cartridge = {}, .voice = {}, .program = 0};
    const auto sysex = attribute(state.xml, kSysexAttribute).and_then(fromJuceBase64);
    const auto voice = attribute(state.xml, kProgramAttribute).and_then(fromJuceBase64);
    const auto program = attribute(state.xml, kCurrentProgramAttribute);
    if (!sysex || sysex->size() != kCartridgeSize || !voice || voice->size() < kUnpackedVoiceSize || !program ||
        std::from_chars(program->data(), program->data() + program->size(), state.program).ec != std::errc{}) {
        return std::nullopt;
    }
    state.cartridge.sysex = *sysex;
    state.voice = *voice;
    return state;
}

std::vector<std::uint8_t> dexedStateWith(const DexedState& state, const Dx7Cartridge& cartridge, std::size_t program) {
    std::vector<std::uint8_t> voice = state.voice;
    std::ranges::copy(unpackedVoice(packedVoice(cartridge, program)), voice.begin());
    std::string xml = state.xml;
    replaceAttribute(xml, kSysexAttribute, juceBase64(cartridge.sysex));
    replaceAttribute(xml, kProgramAttribute, juceBase64(voice));
    replaceAttribute(xml, kCurrentProgramAttribute, std::to_string(program));
    replaceAttribute(xml, kOpSwitchAttribute, "111111");
    std::vector<std::uint8_t> bytes(kJuceXmlMagic.begin(), kJuceXmlMagic.end());
    for (std::size_t shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>(xml.size() >> shift));
    }
    bytes.insert(bytes.end(), xml.begin(), xml.end());
    bytes.push_back(0);
    return bytes;
}

}  // namespace emerick::app
