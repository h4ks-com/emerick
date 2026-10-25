#include "emerick/yamaha/chart.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <iterator>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string_view>

namespace emerick::yamaha {

namespace {

using ChordKey = std::array<std::uint8_t, 4>;
constexpr std::size_t kMainVariations = sectionInfo(Section::Main).variations.size();

core::Ticks nearestBeat(core::Ticks tick, core::Ticks beat) {
    return static_cast<core::Ticks>(std::nearbyint(static_cast<double>(tick) / static_cast<double>(beat))) * beat;
}

core::Ticks placeInChart(core::Ticks tick, const core::TempoMap& tempo, const ChartOptions& options) {
    const core::Ticks shifted = tick - options.shiftBars * tempo.ticksPerBar();
    return std::max<core::Ticks>(0, options.snapToBeat ? nearestBeat(shifted, tempo.ticksPerBeat()) : shifted);
}

// Chords are played a beat early, so a change on the last beat belongs to the next downbeat.
core::Ticks anticipated(core::Ticks at, const core::TempoMap& tempo) {
    const core::Ticks bar = tempo.ticksPerBar();
    return at % bar == bar - tempo.ticksPerBeat() ? (at / bar + 1) * bar : at;
}

std::vector<ChordEvent> collectChords(const std::vector<RawEvent>& events, const core::TempoMap& tempo,
                                      const ChartOptions& options) {
    std::map<core::Ticks, Chord> lastAt;
    std::optional<ChordKey> previous;
    for (const auto& event : events) {
        const auto chord = decodeChordControl(event.bytes);
        if (!chord) {
            continue;
        }
        const ChordKey key{event.bytes[4], event.bytes[5], event.bytes[6], event.bytes[7]};
        if (key != previous) {
            const core::Ticks at = placeInChart(event.at, tempo, options);
            lastAt.insert_or_assign(options.anticipation ? anticipated(at, tempo) : at, *chord);
        }
        previous = key;
    }
    std::vector<ChordEvent> chords;
    chords.reserve(lastAt.size());
    for (const auto& [at, chord] : lastAt) {
        chords.push_back({at, chord});
    }
    return chords;
}

// The keyboard never tells a pattern's length, so we take the largest one every played stretch of a Main fits in
// whole: the greatest common divisor of their lengths in bars. A stretch runs to the next Main, Intro or Ending,
// since the fill or break that ends it still belongs to its last pass.
// ponytail: a single long stretch reads as one long pattern; read the Style file's pattern lengths if that misleads.
void estimatePatternBars(std::vector<SectionEvent>& sections, core::Ticks bar) {
    std::array<int, kMainVariations> patterns{};
    for (auto section = sections.begin(); section != sections.end(); ++section) {
        const auto next = std::find_if(std::next(section), sections.end(),
                                       [](const SectionEvent& event) { return startsSection(event.kind); });
        if (section->kind != Section::Main || next == sections.end()) {
            continue;
        }
        if (const auto played = static_cast<int>((next->at - section->at) / bar); played > 0) {
            auto& pattern = patterns.at(static_cast<std::size_t>(section->variation));
            pattern = std::gcd(pattern, played);
        }
    }
    for (SectionEvent& section : sections) {
        if (section.kind == Section::Main) {
            section.bars = patterns.at(static_cast<std::size_t>(section.variation));
        }
    }
}

std::vector<SectionEvent> collectSections(const std::vector<RawEvent>& events, const core::TempoMap& tempo,
                                          const ChartOptions& options) {
    std::vector<std::pair<core::Ticks, SectionPress>> presses;
    for (const auto& event : events) {
        if (const auto press = decodeSectionControl(event.bytes)) {
            presses.emplace_back(placeInChart(event.at, tempo, options), *press);
        }
    }
    std::vector<SectionEvent> sections;
    const auto firstMain = std::ranges::find_if(presses, [](const auto& p) { return p.second.kind == Section::Main; });
    int current = -1;
    if (firstMain != presses.end()) {
        current = firstMain->second.variation;
        sections.push_back({0, Section::Main, current});
    }
    // While playing, the keyboard reports a Main press as the fill to that Main and then the Main at the next bar line,
    // and after an Ending it reports the Main it stops on.
    bool awaitingStop = false;
    for (const auto& [at, press] : presses) {
        if (!press.on) {
            continue;
        }
        switch (press.kind) {
            case Section::Main:
                if (!awaitingStop && (sections.empty() || sections.back().kind != Section::Main ||
                                      sections.back().variation != press.variation)) {
                    sections.push_back({at, Section::Main, press.variation});
                }
                current = press.variation;
                break;
            case Section::FillTo:
                sections.push_back({at, press.variation == current ? Section::Fill : Section::FillTo, press.variation});
                break;
            case Section::Break:
                sections.push_back({at, Section::Break, 0});
                break;
            case Section::Intro:
            case Section::Ending:
            case Section::Fill:
                sections.push_back({at, press.kind, press.variation});
                break;
        }
        awaitingStop = press.kind == Section::Ending;
    }
    estimatePatternBars(sections, tempo.ticksPerBar());
    return sections;
}

std::string position(core::Ticks tick, const core::TempoMap& tempo) {
    const auto bbt = tempo.toBarBeatTick(tick);
    return std::format("{:>7}", std::to_string(bbt.bar) + "." + std::to_string(bbt.beat));
}

std::pair<int, int> barAndBeat(core::Ticks tick, const core::TempoMap& tempo) {
    const auto bbt = tempo.toBarBeatTick(tick);
    return {bbt.bar, bbt.beat};
}

}  // namespace

void to_json(nlohmann::json& json, const ChordEvent& event) {
    json = {{"at", event.at}, {"chord", event.chord.name()}};
}

void from_json(const nlohmann::json& json, ChordEvent& event) {
    const auto chord = Chord::parse(json.at("chord").get<std::string>());
    if (!chord) {
        throw std::invalid_argument("unknown chord: " + json.at("chord").get<std::string>());
    }
    event = {json.at("at").get<core::Ticks>(), *chord};
}

void to_json(nlohmann::json& json, const SectionEvent& event) {
    json = {{"at", event.at}, {"kind", sectionInfo(event.kind).json}, {"variation", event.variation}};
    if (event.bars > 0) {
        json["bars"] = event.bars;
    }
}

void from_json(const nlohmann::json& json, SectionEvent& event) {
    const auto kind = json.at("kind").get<std::string>();
    const auto* const found = std::ranges::find(kSections, kind, &SectionInfo::json);
    if (found == kSections.end()) {
        throw std::invalid_argument("unknown section: " + kind);
    }
    event = {json.at("at").get<core::Ticks>(), found->kind, json.at("variation").get<int>(), json.value("bars", 0)};
    if (event.bars < 0) {
        throw std::invalid_argument("a pattern length must be positive");
    }
}

Chart chartFromTake(const std::vector<RawEvent>& events, const core::TempoMap& tempo, const ChartOptions& options) {
    return {collectChords(events, tempo, options), collectSections(events, tempo, options)};
}

std::string sectionName(const SectionEvent& event) {
    const SectionInfo& info = sectionInfo(event.kind);
    const std::string_view variation = info.variations.empty()
                                           ? std::string_view{}
                                           : info.variations.substr(static_cast<std::size_t>(event.variation), 1);
    return std::vformat(info.display, std::make_format_args(variation));
}

std::string chartText(const Chart& chart, const core::TempoMap& tempo) {
    struct Line {
        std::pair<int, int> barAndBeat;
        std::string text;
    };
    std::vector<Line> lines;
    lines.reserve(chart.chords.size() + chart.sections.size());
    for (const auto& event : chart.chords) {
        lines.push_back({barAndBeat(event.at, tempo), position(event.at, tempo) + "  " + event.chord.name()});
    }
    for (const auto& event : chart.sections) {
        lines.push_back({barAndBeat(event.at, tempo), position(event.at, tempo) + "  [" + sectionName(event) + "]"});
    }
    std::ranges::stable_sort(lines, {}, &Line::barAndBeat);
    std::string text;
    for (const auto& line : lines) {
        text += line.text + "\n";
    }
    return text;
}

}  // namespace emerick::yamaha
