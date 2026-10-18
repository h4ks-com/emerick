#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <libremidi/reader.hpp>
#include <string>
#include <vector>

namespace emerick::app::test {

// Ticks come back absolute.
inline libremidi::reader readMidiFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    libremidi::reader reader(true);
    reader.parse(bytes);
    return reader;
}

// The texts of one meta type in a track, in order; our texts are short, so the length takes one byte.
inline std::vector<std::string> metaTexts(const libremidi::midi_track& track, libremidi::meta_event_type type) {
    std::vector<std::string> texts;
    for (const auto& event : track) {
        const auto& bytes = event.m.bytes;
        if (bytes.size() >= 3 && event.m.get_meta_event_type() == type) {
            texts.emplace_back(bytes.begin() + 3, bytes.end());
        }
    }
    return texts;
}

}  // namespace emerick::app::test
