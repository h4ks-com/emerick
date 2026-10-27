// Store wave blobs: an IFF FORM of type YTWV whose chunks follow the AIFF layout, big-endian throughout.

#include "wave.hpp"

#include <algorithm>
#include <array>
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
};

Chunks chunksOf(Bytes blob) {
    Chunks found;
    std::size_t at = kFormHeader;
    while (at + kChunkHeader <= blob.size()) {
        const std::size_t size = std::min<std::size_t>(be32(blob, at + 4), blob.size() - at - kChunkHeader);
        const Bytes body = blob.subspan(at + kChunkHeader, size);
        for (auto [tag, slot] : {std::pair{"COMM", &found.comm}, std::pair{"SSND", &found.ssnd},
                                 std::pair{"MARK", &found.mark}, std::pair{"INST", &found.inst}}) {
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

}  // namespace

std::optional<Wave> decodeWave(Bytes blob) {
    if (!isForm(blob)) {
        return std::nullopt;
    }
    const Chunks chunks = chunksOf(blob);
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
    std::array<std::uint8_t, kChunkHeader> chunk{};
    while (!(comm && ssnd) && readBytes(file, chunk)) {
        comm = comm || tagIs(chunk, 0, "COMM");
        ssnd = ssnd || tagIs(chunk, 0, "SSND");
        const std::uint32_t size = be32(chunk, 4);
        file.seekg(size + (size & 1U), std::ios::cur);
    }
    return comm && ssnd;
}

}  // namespace emerick::voices
