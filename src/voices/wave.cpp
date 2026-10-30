// Store wave blobs: an IFF FORM of type YTWV whose chunks follow the AIFF layout, big-endian throughout.

#include "wave.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <ranges>
#include <string_view>

namespace emerick::voices {

namespace {

using Bytes = std::span<const std::uint8_t>;

constexpr std::size_t kFormHeader = 12;
constexpr std::size_t kChunkHeader = 8;
constexpr std::uint16_t kLinearBits = 16;
constexpr float kFullScale = 32768.0F;

std::uint16_t be16(Bytes bytes, std::size_t at) {
    return static_cast<std::uint16_t>((bytes[at] << 8U) | bytes[at + 1]);
}

std::uint32_t be32(Bytes bytes, std::size_t at) {
    return (std::uint32_t{be16(bytes, at)} << 16U) | be16(bytes, at + 2);
}

bool tagIs(Bytes bytes, std::size_t at, std::string_view tag) {
    return std::ranges::equal(bytes.subspan(at, 4), tag,
                              [](std::uint8_t byte, char letter) { return byte == static_cast<std::uint8_t>(letter); });
}

// istream reads into char, so this one wrapper views our byte buffer as char, which may alias any object.
bool readBytes(std::istream& in, std::span<std::uint8_t> bytes) {
    return static_cast<bool>(
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())));
}

bool isForm(Bytes header) {
    return header.size() >= kFormHeader && tagIs(header, 0, "FORM") && tagIs(header, 8, "YTWV");
}

struct Chunks {
    Bytes comm;
    Bytes ssnd;
    Bytes mark;
    Bytes inst;
    Bytes apcp;
    Bytes wxc1;
};

Chunks chunksOf(Bytes blob) {
    Chunks found;
    std::size_t at = kFormHeader;
    while (at + kChunkHeader <= blob.size()) {
        const std::size_t size = std::min<std::size_t>(be32(blob, at + 4), blob.size() - at - kChunkHeader);
        const Bytes body = blob.subspan(at + kChunkHeader, size);
        for (auto [tag, slot] :
             {std::pair{"COMM", &found.comm}, std::pair{"SSND", &found.ssnd}, std::pair{"MARK", &found.mark},
              std::pair{"INST", &found.inst}, std::pair{"APCp", &found.apcp}, std::pair{"WXC1", &found.wxc1}}) {
            if (tagIs(blob, at, tag)) {
                *slot = body;
            }
        }
        at += kChunkHeader + size + (size & 1U);
    }
    return found;
}

std::optional<std::uint32_t> markerPosition(Bytes mark, std::uint16_t id) {
    if (mark.size() < 2) {
        return std::nullopt;
    }
    std::size_t at = 2;
    for (std::uint16_t index = 0; index < be16(mark, 0) && at + 7 <= mark.size(); ++index) {
        if (be16(mark, at) == id) {
            return be32(mark, at + 2);
        }
        const std::size_t nameBytes = 1U + mark[at + 6];
        at += 6 + nameBytes + (nameBytes & 1U);
    }
    return std::nullopt;
}

// The INST chunk names the sustain loop's play mode and its begin and end markers.
void readLoop(const Chunks& chunks, Wave& wave) {
    constexpr std::size_t kSustainLoop = 8;
    if (chunks.inst.size() < kSustainLoop + 6 || be16(chunks.inst, kSustainLoop) == 0) {
        return;
    }
    const auto begin = markerPosition(chunks.mark, be16(chunks.inst, kSustainLoop + 2));
    const auto end = markerPosition(chunks.mark, be16(chunks.inst, kSustainLoop + 4));
    if (begin && end) {
        wave.loopStart = *begin;
        wave.loopEnd = *end;
    }
}

// WXC1 is Yamaha's adaptive predictive wave compression: each 32-byte frame holds 16 scrambled words of residual codes,
// and carries the predictor coefficients, scale and mode of the frame after it.
constexpr std::size_t kWxcHeader = 68;
constexpr std::size_t kWxcParams = 22;
constexpr std::size_t kFrameBytes = 32;
constexpr std::size_t kFrameWords = 16;
constexpr std::size_t kModeCount = 8;
constexpr int kLastScale = 63;

using Words = std::array<std::uint16_t, kFrameWords>;
using Params = std::array<std::uint16_t, 3>;

template <std::size_t N>
constexpr std::array<int, 2 * N> excitation(std::array<int, N> half) {
    std::array<int, 2 * N> levels{};
    for (std::size_t code = 0; code < N; ++code) {
        levels.at(code) = half.at(code);
        levels.at((2 * N) - 1 - code) = -half.at(code);
    }
    return levels;
}

constexpr auto kLevels2 = excitation(std::array{30, 138});
constexpr auto kLevels3 = excitation(std::array{10, 38, 86, 170});
constexpr auto kLevels4 = excitation(std::array{6, 22, 38, 54, 70, 102, 134, 198});
constexpr auto kLevels6 =
    excitation(std::array{2,  6,  10, 14, 18, 22,  26,  30,  34,  38,  42,  46,  50,  54,  58,  62,
                          66, 74, 82, 90, 98, 106, 114, 122, 130, 146, 162, 178, 194, 210, 226, 242});

using LevelPair = std::array<int, 2>;

// Modes 4 and 7 quantize two samples per code, and their codebooks were fitted against decoded loop histories.
constexpr std::array<LevelPair, 16> kPairs4{{{0, 0},
                                             {22, 22},
                                             {38, 38},
                                             {54, 54},
                                             {70, 70},
                                             {102, 102},
                                             {134, 134},
                                             {198, 198},
                                             {-198, -198},
                                             {-134, -134},
                                             {-102, -102},
                                             {-70, -170},
                                             {-54, -54},
                                             {-38, -38},
                                             {-22, -22},
                                             {-6, -6}}};
constexpr std::array<LevelPair, 8> kPairs7{
    {{16, 10}, {38, 38}, {86, 172}, {170, 170}, {-162, -170}, {-86, -86}, {-38, -38}, {-10, -10}}};

struct Mode {
    unsigned bits;
    std::span<const int> levels;
    std::span<const LevelPair> pairs;
    int boost;
};

// Mode 5 yields 64 samples a frame, as every wave's APCp sample count shows, so we read it as mode 1 with the boost of
// the modes above 4, as mode 6 is mode 2.
constexpr std::array<Mode, kModeCount> kModes{
    Mode{2, kLevels2, {}, 4}, Mode{3, kLevels3, {}, 4}, Mode{4, kLevels4, {}, 4}, Mode{6, kLevels6, {}, 4},
    Mode{4, {}, kPairs4, 8},  Mode{3, kLevels3, {}, 8}, Mode{4, kLevels4, {}, 8}, Mode{3, {}, kPairs7, 8}};

constexpr int scaleFactor(int index) { return (4 + (index % 4)) << (1 + (index / 4)); }

std::uint16_t descramble(std::uint16_t word) {
    const unsigned value = word;
    return static_cast<std::uint16_t>(((value ^ (value << 6U) ^ (value << 12U)) & 0xF000U) | (value & 0x0FFFU));
}

struct FrameParams {
    std::array<int, 3> coefficients;
    int scale;
    std::size_t mode;
};

FrameParams unpack(const Params& packed) {
    const auto signed16 = [](std::uint16_t value) { return int{static_cast<std::int16_t>(value)}; };
    const int third = static_cast<int>(((packed[2] >> 3U) & 0xFFFU) ^ 0x800U) - 0x800;
    return FrameParams{.coefficients = {signed16(packed[0]) >> 3, signed16(packed[1]) >> 3, third},
                       .scale = static_cast<int>(((packed[0] & 7U) << 3U) | (packed[1] & 7U)),
                       .mode = packed[2] & 7U};
}

Params paramsAfter(const Words& words) {
    std::array<unsigned, 3> packed{};
    for (std::size_t word = 0; word < 13; ++word) {
        for (std::size_t field = 0; field < 3; ++field) {
            packed.at(field) |= ((words.at(word) >> (14 - field)) & 1U) << (15 - word);
        }
    }
    packed[0] |= (words[14] >> 12U) & 7U;
    packed[1] |= (words[15] >> 12U) & 7U;
    packed[2] |= (words[13] >> 12U) & 7U;
    return {static_cast<std::uint16_t>(packed[0]), static_cast<std::uint16_t>(packed[1]),
            static_cast<std::uint16_t>(packed[2])};
}

void decodeFrame(const Words& words, const FrameParams& frame, const Mode& mode, std::array<int, 3>& history,
                 std::vector<float>& samples) {
    const auto [a1, a2, a3] = frame.coefficients;
    const unsigned mask = (1U << mode.bits) - 1;
    for (const std::uint16_t word : words) {
        const int scale = scaleFactor(std::min(kLastScale, frame.scale + (mode.boost * (word >> 15U))));
        const auto emit = [&](int level) {
            const int prediction = std::clamp(
                ((history[0] * a1) >> 8) + ((history[1] * a2) >> 8) + ((history[2] * a3) >> 8), -0x20000, 0x1FFFF);
            const int sample = std::clamp((((scale * level) >> 8) + prediction) >> 2, -0x8000, 0x7FFF);
            history = {sample, history[0], history[1]};
            samples.push_back(static_cast<float>(sample) / kFullScale);
        };
        for (unsigned shift = 0; shift + mode.bits <= 12; shift += mode.bits) {
            const unsigned code = (word >> shift) & mask;
            if (mode.pairs.empty()) {
                emit(mode.levels[code]);
            } else {
                emit(mode.pairs[code][0]);
                emit(mode.pairs[code][1]);
            }
        }
    }
}

std::optional<Wave> decodeWxc1(Bytes wxc1, Bytes apcp) {
    if (wxc1.size() < kWxcHeader + kFrameBytes || apcp.size() < 16) {
        return std::nullopt;
    }
    Params params{be16(wxc1, kWxcParams), be16(wxc1, kWxcParams + 2), be16(wxc1, kWxcParams + 4)};
    std::array<int, 3> history{};
    Wave wave;
    std::vector<std::uint32_t> frameStarts;
    for (std::size_t at = kWxcHeader; at + kFrameBytes <= wxc1.size(); at += kFrameBytes) {
        frameStarts.push_back(static_cast<std::uint32_t>(wave.samples.size()));
        const FrameParams frame = unpack(params);
        Words words{};
        for (std::size_t word = 0; word < kFrameWords; ++word) {
            words.at(word) = descramble(be16(wxc1, at + (2 * word)));
        }
        decodeFrame(words, frame, kModes.at(frame.mode), history, wave.samples);
        params = paramsAfter(words);
    }
    // The header names the loop by frame: it runs from the start of its first frame to the end of its last.
    const std::uint32_t loopFirst = be32(wxc1, 6);
    const std::uint32_t loopLast = be32(wxc1, 10);
    frameStarts.push_back(static_cast<std::uint32_t>(wave.samples.size()));
    if (loopFirst <= loopLast && loopLast + 1 < frameStarts.size()) {
        wave.loopStart = frameStarts[loopFirst];
        wave.loopEnd = frameStarts[loopLast + 1];
    }
    return wave;
}

}  // namespace

std::optional<Wave> decodeWave(Bytes blob) {
    if (!isForm(blob)) {
        return std::nullopt;
    }
    const Chunks chunks = chunksOf(blob);
    if (!chunks.wxc1.empty()) {
        return decodeWxc1(chunks.wxc1, chunks.apcp);
    }
    constexpr std::size_t kSoundOffset = 8;
    if (chunks.comm.size() < 8 || chunks.ssnd.size() < kSoundOffset || be16(chunks.comm, 6) != kLinearBits ||
        be16(chunks.comm, 0) == 0) {
        return std::nullopt;
    }
    const std::size_t channels = be16(chunks.comm, 0);
    const std::size_t start = kSoundOffset + be32(chunks.ssnd, 0);
    const std::size_t available = chunks.ssnd.size() > start ? (chunks.ssnd.size() - start) / (2 * channels) : 0;
    const std::size_t frames = std::min<std::size_t>(be32(chunks.comm, 2), available);
    Wave wave;
    wave.samples = std::views::iota(std::size_t{0}, frames) | std::views::transform([&](std::size_t frame) {
                       const auto sample = static_cast<std::int16_t>(be16(chunks.ssnd, start + frame * 2 * channels));
                       return static_cast<float>(sample) / kFullScale;
                   }) |
                   std::ranges::to<std::vector>();
    readLoop(chunks, wave);
    return wave;
}

std::optional<Wave> loadWave(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> blob(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    return readBytes(file, blob) ? decodeWave(blob) : std::nullopt;
}

bool isDecodableWave(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::array<std::uint8_t, kFormHeader> header{};
    if (!readBytes(file, header) || !isForm(header)) {
        return false;
    }
    bool comm = false;
    bool ssnd = false;
    bool apcp = false;
    bool wxc1 = false;
    std::array<std::uint8_t, kChunkHeader> chunk{};
    while (!(comm && ssnd) && !(apcp && wxc1) && readBytes(file, chunk)) {
        const std::uint32_t size = be32(chunk, 4);
        const std::streampos next = file.tellg() + std::streamoff{size + (size & 1U)};
        comm = comm || tagIs(chunk, 0, "COMM");
        ssnd = ssnd || tagIs(chunk, 0, "SSND");
        apcp = apcp || tagIs(chunk, 0, "APCp");
        wxc1 = wxc1 || (tagIs(chunk, 0, "WXC1") && size >= kWxcHeader + kFrameBytes);
        file.seekg(next);
    }
    return (comm && ssnd) || (apcp && wxc1);
}

}  // namespace emerick::voices
