#include "emerick/app/midi_import.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <libremidi/reader.hpp>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "emerick/app/registry.hpp"
#include "emerick/core/midi.hpp"
#include "emerick/model/midi_notes.hpp"

namespace emerick::app {

namespace {

constexpr std::uint8_t kMeta = 0xFF;
constexpr std::uint8_t kMoreBytes = 0x80;
constexpr std::size_t kMetaLengthStart = 2;

// A meta event holds 0xFF, its type, a variable length quantity and the text.
std::optional<std::string> metaText(const libremidi::message& message, libremidi::meta_event_type type) {
    const auto& bytes = message.bytes;
    if (bytes.size() <= kMetaLengthStart || bytes[0] != kMeta || bytes[1] != std::to_underlying(type)) {
        return std::nullopt;
    }
    auto text = bytes.begin() + kMetaLengthStart;
    while (text != bytes.end() && (*text & kMoreBytes) != 0) {
        ++text;
    }
    if (text == bytes.end()) {
        return std::nullopt;
    }
    return std::string{std::next(text), bytes.end()};
}

std::string trackName(const libremidi::midi_track& track) {
    for (const libremidi::track_event& event : track) {
        if (auto name = metaText(event.m, libremidi::meta_event_type::TRACK_NAME)) {
            return *name;
        }
    }
    return {};
}

// Each lyric goes to the first note that starts on its tick.
void attachLyrics(model::MidiClip& clip, std::map<core::Ticks, std::string>& lyrics) {
    for (model::Note& note : clip.notes) {
        if (auto lyric = lyrics.extract(note.start)) {
            note.lyric = std::move(lyric.mapped());
        }
    }
}

bool playsNote(const model::MidiEvent& event) {
    return event.bytes.size() >= 3 && (event.bytes[0] & 0xF0) == core::midi::kNoteOn && event.bytes[2] > 0;
}

}  // namespace

std::vector<MidiPart> readMidiParts(const std::filesystem::path& file, const std::string& fallbackName,
                                    core::Ticks ticksPerBar) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw AppError("cannot open " + file.string());
    }
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    libremidi::reader reader(true);
    if (reader.parse(bytes) == libremidi::reader::invalid || reader.ticksPerBeat <= 0) {
        throw AppError(file.filename().string() + " is not a MIDI file we can read");
    }
    if (reader.format > 1) {
        throw AppError(file.filename().string() + " is a type 2 MIDI file; convert it to type 0 or 1 first");
    }
    const double scale = static_cast<double>(core::kTicksPerQuarter) / reader.ticksPerBeat;
    std::vector<MidiPart> parts;
    for (const libremidi::midi_track& track : reader.tracks) {
        std::map<int, std::vector<model::MidiEvent>> channels;
        std::vector<model::MidiEvent> sysex;
        std::map<core::Ticks, std::string> lyrics;
        for (const libremidi::track_event& event : track) {
            const auto& message = event.m.bytes;
            if (message.empty()) {
                continue;
            }
            const core::Ticks at = std::llround(event.tick * scale);
            if (auto lyric = metaText(event.m, libremidi::meta_event_type::LYRIC)) {
                lyrics.emplace(at, std::move(*lyric));
                continue;
            }
            model::MidiEvent converted{.at = at, .bytes = message};
            if (core::midi::isChannelMessage(message[0])) {
                channels[(message[0] & 0x0F) + 1].push_back(std::move(converted));
            } else if (message[0] == core::midi::kSysexStart) {
                sysex.push_back(std::move(converted));
            }
        }
        std::erase_if(channels, [](const auto& entry) { return !std::ranges::any_of(entry.second, playsNote); });
        std::string name = trackName(track);
        if (name.empty()) {
            name = fallbackName;
        }
        for (auto& [channel, events] : channels) {
            MidiPart part{.name = channels.size() > 1 ? name + " ch " + std::to_string(channel) : name, .clip = {}};
            part.clip.events = std::move(events);
            // The track's first part takes its sysex, such as a Style's chord and section messages.
            std::ranges::move(std::exchange(sysex, {}), std::back_inserter(part.clip.events));
            const core::Ticks last = std::ranges::max(part.clip.events, {}, &model::MidiEvent::at).at;
            part.clip.length = (last / ticksPerBar + 1) * ticksPerBar;
            model::gatherNotes(part.clip);
            attachLyrics(part.clip, lyrics);
            parts.push_back(std::move(part));
        }
    }
    return parts;
}

}  // namespace emerick::app
