#include "emerick/arranger/style.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "casm.hpp"
#include "emerick/core/midi.hpp"
#include "emerick/yamaha/section.hpp"
#include "reader.hpp"

namespace emerick::arranger {

namespace {

namespace midi = core::midi;
using yamaha::Section;

constexpr std::uint8_t kMetaEvent = 0xFF;
constexpr std::uint8_t kMetaMarker = 0x06;
constexpr std::uint8_t kMetaEndOfTrack = 0x2F;
constexpr std::uint8_t kMetaTempo = 0x51;
constexpr std::uint8_t kMetaTimeSignature = 0x58;
constexpr std::size_t kHeaderLength = 6;
constexpr double kMicrosecondsPerMinute = 60'000'000.0;

int dataByteCount(std::uint8_t status) {
    const auto type = static_cast<std::uint8_t>(status & 0xF0);
    return type == midi::kProgramChange || type == midi::kChannelPressure ? 1 : 2;
}

struct SetupChannel {
    PartVoice voice;
    bool hasProgram = false;
    bool hasBank = false;
};

class TrackParser {
public:
    TrackParser(Style& style, std::array<SetupChannel, kChannels>& setup) : style_(&style), setup_(&setup) {}

    void parse(std::span<const std::uint8_t> body) {
        Reader reader(body);
        std::int64_t tick = 0;
        std::uint8_t running = 0;
        while (!reader.atEnd()) {
            tick += reader.vlq();
            std::uint8_t status = reader.peek();
            if (status < midi::kNoteOff) {
                if (running == 0) {
                    throw StyleError("The style has a data byte with no status before it.");
                }
                status = running;
            } else {
                reader.byte();
            }
            if (status == kMetaEvent) {
                const std::uint8_t type = reader.byte();
                const auto data = reader.take(reader.vlq());
                if (type == kMetaEndOfTrack) {
                    break;
                }
                meta(type, data, tick);
            } else if (status == midi::kSysexStart || status == midi::kSysexEnd) {
                reader.take(reader.vlq());
            } else if (midi::isChannelMessage(status)) {
                running = status;
                StyleEvent event{.tick = tick, .bytes = {status, 0, 0}};
                for (int index = 1; index <= dataByteCount(status); ++index) {
                    event.bytes.at(static_cast<std::size_t>(index)) = reader.dataByte();
                }
                channelMessage(event);
            } else {
                throw StyleError("The style has a system message inside its track.");
            }
        }
        closeSection(tick);
    }

private:
    enum class Region : std::uint8_t { Setup, Section, Ignored };

    void meta(std::uint8_t type, std::span<const std::uint8_t> data, std::int64_t tick) {
        if (type == kMetaMarker) {
            marker(textOf(data), tick);
        } else if (type == kMetaTempo && data.size() == 3 && region_ == Region::Setup) {
            const int microseconds = (data[0] << 16) | (data[1] << 8) | data[2];
            if (microseconds > 0) {
                style_->bpm = kMicrosecondsPerMinute / microseconds;
            }
        } else if (type == kMetaTimeSignature && data.size() >= 2 && region_ == Region::Setup) {
            if (data[0] == 0 || data[1] > 6) {
                throw StyleError("The style has a time signature no keyboard plays.");
            }
            style_->meter = {.numerator = data[0], .denominator = 1 << data[1]};
        }
    }

    void marker(const std::string& text, std::int64_t tick) {
        const auto name = sectionName(text);
        if (!name) {
            if (region_ == Region::Section) {
                closeSection(tick);
                region_ = Region::Ignored;
            }
            return;
        }
        closeSection(tick);
        region_ = Region::Section;
        sectionStart_ = tick;
        open_ = StyleSection{.kind = name->kind, .variation = name->variation, .length = 0, .events = {}};
    }

    void closeSection(std::int64_t tick) {
        if (!open_) {
            return;
        }
        open_->length = tick - sectionStart_;
        const bool duplicate = findSection(*style_, open_->kind, open_->variation) != nullptr;
        if (open_->length > 0 && !duplicate) {
            style_->sections.push_back(std::move(*open_));
        }
        open_.reset();
    }

    void channelMessage(StyleEvent event) {
        if (region_ == Region::Section && open_) {
            event.tick -= sectionStart_;
            open_->events.push_back(event);
        } else if (region_ == Region::Setup) {
            setupMessage(event.bytes);
        }
    }

    void setupMessage(const std::array<std::uint8_t, 3>& bytes) {
        auto& channel = setup_->at(bytes[0] & 0x0F);
        const auto type = static_cast<std::uint8_t>(bytes[0] & 0xF0);
        if (type == midi::kProgramChange) {
            if (!channel.hasBank) {
                // A program without a bank select means bank 0, so we send it to clear the previous style's bank.
                const auto control = static_cast<std::uint8_t>(midi::kControlChange | (bytes[0] & 0x0F));
                channel.voice.setup.push_back({control, midi::kBankSelectCc, 0});
                channel.voice.setup.push_back({control, midi::kBankSelectLsbCc, 0});
                channel.hasBank = true;
            }
            channel.voice.program = bytes[1];
            channel.hasProgram = true;
        } else if (type == midi::kControlChange) {
            if (bytes[1] == midi::kBankSelectCc) {
                channel.hasBank = true;
                channel.voice.bankMsb = bytes[2];
            } else if (bytes[1] == midi::kBankSelectLsbCc) {
                channel.hasBank = true;
                channel.voice.bankLsb = bytes[2];
            }
        } else {
            return;
        }
        channel.voice.setup.push_back(bytes);
    }

    Style* style_;
    std::array<SetupChannel, kChannels>* setup_;
    Region region_ = Region::Setup;
    std::int64_t sectionStart_ = 0;
    std::optional<StyleSection> open_;
};

void readHeader(std::span<const std::uint8_t> body, Style& style) {
    if (body.size() < kHeaderLength) {
        throw StyleError("The style's MIDI header is too short.");
    }
    const int division = (body[4] << 8) | body[5];
    if (division == 0 || (division & 0x8000) != 0) {
        throw StyleError("The style counts time in SMPTE frames or not at all; a style counts in quarter notes.");
    }
    style.ticksPerQuarter = division;
}

void fillDefaultRules(StyleSection& section) {
    for (int channel = kFirstPartChannel; channel < kChannels; ++channel) {
        auto& rule = section.rules.at(static_cast<std::size_t>(channel));
        if (!rule) {
            rule = defaultRule(channel);
        }
    }
}

void assignVoices(Style& style, const std::array<SetupChannel, kChannels>& setup) {
    const StyleSection* first = style.sections.empty() ? nullptr : &style.sections.front();
    for (int channel = 0; channel < kChannels; ++channel) {
        const auto& source = setup.at(static_cast<std::size_t>(channel));
        const auto& rule = (first != nullptr) ? first->rules.at(static_cast<std::size_t>(channel)) : std::nullopt;
        if (!source.hasProgram || !rule) {
            continue;
        }
        const int part = rule->destination - kFirstPartChannel;
        if (part >= 0 && part < kParts) {
            style.voices.at(static_cast<std::size_t>(part)) = source.voice;
        }
    }
}

}  // namespace

Style parseStyle(std::span<const std::uint8_t> bytes) {
    const auto all = chunks(bytes);
    if (all.empty() || all.front().id != fourCc("MThd")) {
        throw StyleError("The file is no style: it does not start with a MIDI header.");
    }
    Style style;
    readHeader(all.front().body, style);
    std::array<SetupChannel, kChannels> setup{};
    const auto track = std::ranges::find(all, fourCc("MTrk"), &Chunk::id);
    if (track == all.end()) {
        throw StyleError("The style has no MIDI track.");
    }
    TrackParser(style, setup).parse(track->body);
    if (style.sections.empty()) {
        throw StyleError("The style has no section marker such as Main A.");
    }
    for (const auto& chunk : all) {
        if (chunk.id == fourCc("CASM")) {
            readCasm(chunk.body, style);
        }
    }
    std::ranges::for_each(style.sections, fillDefaultRules);
    assignVoices(style, setup);
    return style;
}

const StyleSection* findSection(const Style& style, Section kind, int variation) {
    const auto found = std::ranges::find_if(style.sections, [&](const StyleSection& section) {
        return section.kind == kind && section.variation == variation;
    });
    return found == style.sections.end() ? nullptr : &*found;
}

std::array<std::map<int, int>, kParts> partNotes(const StyleSection& section) {
    std::array<std::map<int, int>, kParts> notes;
    for (const StyleEvent& event : section.events) {
        const std::optional<ChannelRule>& rule = section.rules.at(event.bytes[0] & 0x0FU);
        const int part = rule ? rule->destination - kFirstPartChannel : -1;
        if ((event.bytes[0] & 0xF0U) == midi::kNoteOn && event.bytes[2] > 0 && part >= 0 && part < kParts) {
            ++notes.at(static_cast<std::size_t>(part))[event.bytes[1]];
        }
    }
    return notes;
}

std::string styleHash(std::span<const std::uint8_t> bytes) {
    constexpr std::uint64_t kOffsetBasis = 0xCBF29CE484222325;
    constexpr std::uint64_t kPrime = 0x100000001B3;
    const std::uint64_t hash =
        std::accumulate(bytes.begin(), bytes.end(), kOffsetBasis,
                        [](std::uint64_t value, std::uint8_t byte) { return (value ^ byte) * kPrime; });
    return std::format("{:016x}", hash);
}

}  // namespace emerick::arranger
