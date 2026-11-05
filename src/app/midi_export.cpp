#include "emerick/app/midi_export.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <libremidi/writer.hpp>
#include <map>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "emerick/core/midi.hpp"

namespace emerick::app {

namespace {

constexpr std::uint8_t kMeta = 0xFF;
constexpr std::uint8_t kSevenBits = 0x7F;
constexpr std::uint8_t kMoreBytes = 0x80;
constexpr int kClocksPerClick = 24;
constexpr int kThirtySecondsPerQuarter = 8;
constexpr double kMicrosecondsPerMinute = 60'000'000.0;

using Bytes = std::vector<std::uint8_t>;

struct TimedBytes {
    core::Ticks at = 0;
    Bytes bytes;
};

// A meta event's length is a variable length quantity: seven bits per byte, the high bit set on all but the last.
Bytes metaText(libremidi::meta_event_type type, std::string_view text) {
    std::size_t size = text.size();
    Bytes length{static_cast<std::uint8_t>(size & kSevenBits)};
    while ((size >>= 7) > 0) {
        length.insert(length.begin(), static_cast<std::uint8_t>((size & kSevenBits) | kMoreBytes));
    }
    Bytes bytes{kMeta, std::to_underlying(type)};
    bytes.insert(bytes.end(), length.begin(), length.end());
    bytes.insert(bytes.end(), text.begin(), text.end());
    return bytes;
}

bool belongsInFile(const Bytes& bytes) {
    return !bytes.empty() && (core::midi::isChannelMessage(bytes[0]) || bytes[0] == core::midi::kSysexStart);
}

Bytes onChannel(Bytes bytes, int channel) {
    if (channel != 0 && core::midi::isChannelMessage(bytes[0])) {
        bytes[0] = core::midi::statusFor(bytes[0] & 0xF0, channel);
    }
    return bytes;
}

void addTrack(libremidi::writer& writer, int index, std::vector<TimedBytes> events) {
    std::ranges::stable_sort(events, {}, &TimedBytes::at);
    core::Ticks previous = 0;
    for (TimedBytes& event : events) {
        libremidi::message message;
        message.bytes = std::move(event.bytes);
        writer.add_event(static_cast<int>(event.at - previous), index, message);
        previous = event.at;
    }
}

std::vector<TimedBytes> conductor(const engine::Session& session, const model::Document& document,
                                  const Registry& registry, const std::string& title) {
    const auto microsecondsPerQuarter = static_cast<int>(kMicrosecondsPerMinute / document.tempo.bpm);
    std::vector<TimedBytes> events{
        {0, metaText(libremidi::meta_event_type::TRACK_NAME, title)},
        {0, libremidi::meta_events::tempo(microsecondsPerQuarter).bytes},
        {0, libremidi::meta_events::time_signature(document.tempo.meter.numerator, document.tempo.meter.denominator,
                                                   kClocksPerClick, kThirtySecondsPerQuarter)
                .bytes}};
    std::map<core::Ticks, std::string> markers;
    const auto addMarker = [&](core::Ticks at, const std::string& name) {
        std::string& text = markers[at];
        text += text.empty() ? name : ", " + name;
    };
    for (const model::Marker& marker : document.markers) {
        addMarker(marker.at, marker.name);
    }
    for (const engine::Track& track : session.tracks) {
        const model::Track* source = model::findTrack(document, model::TrackId{track.id});
        const TrackType* type = source == nullptr ? nullptr : registry.trackType(source->kind);
        if (type == nullptr || !type->markers) {
            continue;
        }
        for (const Marker& marker : type->markers(*source)) {
            addMarker(marker.at, marker.text);
        }
    }
    for (const auto& [at, text] : markers) {
        events.push_back({at, metaText(libremidi::meta_event_type::MARKER, text)});
    }
    return events;
}

// Each lyric sits just before its note-on, where OpenUtau and karaoke players read it.
std::vector<TimedBytes> lyricEvents(const model::Track& source) {
    std::vector<TimedBytes> events;
    for (const model::Clip& clip : source.clips) {
        const auto* midi = std::get_if<model::MidiClip>(&clip);
        if (midi == nullptr || midi->muted || midi->length <= 0) {
            continue;
        }
        const core::Ticks plays = std::max(midi->length, midi->loopLength);
        for (core::Ticks pass = 0; pass < plays; pass += midi->length) {
            for (const model::Note& note : midi->notes) {
                if (!note.lyric.empty() && pass + note.start < plays) {
                    events.push_back(
                        {midi->start + pass + note.start, metaText(libremidi::meta_event_type::LYRIC, note.lyric)});
                }
            }
        }
    }
    return events;
}

std::vector<TimedBytes> trackEvents(const engine::Track& track, const model::Track& source) {
    std::vector<TimedBytes> events{{0, metaText(libremidi::meta_event_type::TRACK_NAME, source.name)}};
    std::ranges::move(lyricEvents(source), std::back_inserter(events));
    for (const engine::MidiClip& clip : track.midiClips) {
        for (const engine::MidiEvent& event : clip.events) {
            if (belongsInFile(event.bytes)) {
                events.push_back({clip.start + event.tick, onChannel(event.bytes, source.midiOut.channel)});
            }
        }
    }
    return events;
}

}  // namespace

void exportMidi(const engine::Session& session, const model::Document& document, const Registry& registry,
                const std::string& title, const std::filesystem::path& file) {
    libremidi::writer writer;
    writer.ticksPerQuarterNote = static_cast<std::uint16_t>(core::kTicksPerQuarter);
    addTrack(writer, 0, conductor(session, document, registry, title));
    int index = 1;
    for (const engine::Track& track : session.tracks) {
        const model::Track* source = model::findTrack(document, model::TrackId{track.id});
        if (track.kind == engine::TrackKind::midi && source != nullptr) {
            addTrack(writer, index++, trackEvents(track, *source));
        }
    }
    if (!file.parent_path().empty()) {
        std::filesystem::create_directories(file.parent_path());
    }
    std::ofstream out(file, std::ios::binary);
    writer.write(out);
    out.close();
    if (!out) {
        throw AppError("cannot write " + file.string());
    }
}

}  // namespace emerick::app
