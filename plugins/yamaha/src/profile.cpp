#include "emerick/yamaha/profile.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "emerick/core/midi.hpp"

namespace emerick::yamaha {

namespace {

using core::midi::kControlChange;
using core::midi::kProgramChange;
using core::midi::statusFor;

constexpr std::uint8_t kBankMsbCc = 0;
constexpr std::uint8_t kBankLsbCc = 32;
constexpr std::uint8_t kVolumeCc = 7;
constexpr std::uint8_t kPanCc = 10;

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    return first == std::string_view::npos ? std::string_view{}
                                           : text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

struct VoiceField {
    std::string_view name;
    int Voice::* field;
    int low;
    int high;
};

// The order is the order of a voice line's numbers.
constexpr std::array kVoiceFields{
    VoiceField{"channel", &Voice::channel, 1, 16},  VoiceField{"bankMsb", &Voice::bankMsb, 0, 127},
    VoiceField{"bankLsb", &Voice::bankLsb, 0, 127}, VoiceField{"program", &Voice::program, 1, 128},
    VoiceField{"volume", &Voice::volume, 0, 127},   VoiceField{"pan", &Voice::pan, 0, 127},
};

int inRange(const VoiceField& field, int value) {
    if (value < field.low || value > field.high) {
        throw std::invalid_argument("voice " + std::string(field.name) + " must be " + std::to_string(field.low) +
                                    " to " + std::to_string(field.high) + ", got " + std::to_string(value));
    }
    return value;
}

int takeNumber(std::string_view& line, const VoiceField& field) {
    line = trim(line);
    const auto end = line.find_first_of(" \t");
    const auto word = line.substr(0, end);
    int value = 0;
    const auto [rest, error] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (word.empty() || error != std::errc{} || rest != word.data() + word.size()) {
        throw std::invalid_argument("bad voice line field: " + std::string(word));
    }
    line = end == std::string_view::npos ? std::string_view{} : line.substr(end);
    return inRange(field, value);
}

Voice parseVoice(std::string_view line) {
    Voice voice;
    for (const VoiceField& field : kVoiceFields) {
        voice.*field.field = takeNumber(line, field);
    }
    voice.name = std::string(trim(line));
    return voice;
}

}  // namespace

const std::array<Part, 16>& genos2Parts() {
    static const std::array<Part, 16> parts{{
        {1, "Right 1", "piano-keys"},
        {2, "Left", "piano-keys"},
        {3, "Right 2", "piano-keys"},
        {4, "Right 3", "piano-keys"},
        {5, "Multi Pad 1", "squares-four"},
        {6, "Multi Pad 2", "squares-four"},
        {7, "Multi Pad 3", "squares-four"},
        {8, "Multi Pad 4", "squares-four"},
        {9, "Rhythm 1", "drum"},
        {10, "Rhythm 2", "drum"},
        {11, "Bass", "guitar"},
        {12, "Chord 1", "music-notes"},
        {13, "Chord 2", "music-notes"},
        {14, "Pad", "waveform"},
        {15, "Phrase 1", "music-note"},
        {16, "Phrase 2", "music-note"},
    }};
    return parts;
}

std::vector<Voice> parseVoices(std::string_view text) {
    std::vector<Voice> voices;
    std::istringstream stream{std::string(text)};
    std::string line;
    while (std::getline(stream, line)) {
        const auto content = trim(line);
        if (!content.empty() && content.front() != '#') {
            voices.push_back(parseVoice(content));
        }
    }
    return voices;
}

std::vector<Voice> loadVoices(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::ostringstream text;
    text << file.rdbuf();
    return parseVoices(text.str());
}

void to_json(nlohmann::json& json, const Voice& voice) {
    json = {{"name", voice.name}};
    for (const VoiceField& field : kVoiceFields) {
        json[std::string(field.name)] = voice.*field.field;
    }
}

void from_json(const nlohmann::json& json, Voice& voice) {
    voice.name = json.at("name").get<std::string>();
    for (const VoiceField& field : kVoiceFields) {
        voice.*field.field = inRange(field, json.at(std::string(field.name)).get<int>());
    }
}

std::vector<RawEvent> voiceMessages(const std::vector<Voice>& voices, core::Ticks at, core::Ticks gapTicks) {
    std::vector<RawEvent> events;
    const auto add = [&](std::vector<std::uint8_t> bytes) {
        events.push_back({at + static_cast<core::Ticks>(events.size()) * gapTicks, std::move(bytes)});
    };
    for (const auto& voice : voices) {
        const auto control = statusFor(kControlChange, voice.channel);
        add({control, kBankMsbCc, static_cast<std::uint8_t>(voice.bankMsb)});
        add({control, kBankLsbCc, static_cast<std::uint8_t>(voice.bankLsb)});
        add({statusFor(kProgramChange, voice.channel), static_cast<std::uint8_t>(voice.program - 1)});
        add({control, kVolumeCc, static_cast<std::uint8_t>(voice.volume)});
        add({control, kPanCc, static_cast<std::uint8_t>(voice.pan)});
    }
    return events;
}

}  // namespace emerick::yamaha
