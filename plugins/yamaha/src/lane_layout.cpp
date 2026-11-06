#include "emerick/yamaha/lane_layout.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "emerick/yamaha/chart.hpp"

namespace emerick::yamaha {

namespace {

// Indexes into the theme's track colors: Main A to D take the first four, then Fill, Break, Intro and Ending.
constexpr std::array kSectionColors{0, 4, 4, 5, 6, 7};
static_assert(kSectionColors.size() == kSections.size());

template <class Event>
std::vector<Event> eventsOrNone(const nlohmann::json& data) {
    try {
        return sortedEvents<Event>(data);
    } catch (const nlohmann::json::exception&) {
        return {};
    } catch (const std::invalid_argument&) {
        return {};
    }
}

template <class Event, class Describe>
std::vector<LaneBlock> blocks(const nlohmann::json& data, Describe describe) {
    const std::vector<Event> events = eventsOrNone<Event>(data);
    std::vector<LaneBlock> result;
    for (std::size_t index = 0; index < events.size(); ++index) {
        LaneBlock block = describe(events[index]);
        block.at = events[index].at;
        block.end = index + 1 < events.size() ? events[index + 1].at : block.at + kLastBlockTicks;
        result.push_back(std::move(block));
    }
    return result;
}

LaneBlock sectionBlock(const SectionEvent& event) {
    const int base = kSectionColors.at(static_cast<std::size_t>(event.kind));
    return LaneBlock{.at = event.at,
                     .end = event.at,
                     .label = sectionName(event),
                     .gap = false,
                     .colorIndex = event.kind == Section::Main ? base + event.variation : base,
                     .patternBars = event.bars};
}

// The Main a fill or a break leads into, or nullopt when the Arranger plays the event as it stands.
std::optional<SectionEvent> landing(const SectionEvent& event, const std::optional<SectionEvent>& playingMain,
                                    bool autoFill) {
    if (!playingMain) {
        return std::nullopt;
    }
    switch (event.kind) {
        case Section::Main:
            return autoFill ? std::optional{event} : std::nullopt;
        case Section::FillTo:
            return SectionEvent{.at = event.at, .kind = Section::Main, .variation = event.variation, .bars = 0};
        case Section::Fill:
        case Section::Break:
            return playingMain;
        case Section::Intro:
        case Section::Ending:
            return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace

void to_json(nlohmann::json& json, const LaneBlock& block) {
    json = {{"at", block.at},
            {"end", block.end},
            {"label", block.label},
            {"gap", block.gap},
            {"colorIndex", block.colorIndex},
            {"patternBars", block.patternBars}};
}

std::vector<LaneBlock> chordBlocks(const nlohmann::json& data) {
    return blocks<ChordEvent>(data, [](const ChordEvent& event) {
        return LaneBlock{.at = 0,
                         .end = 0,
                         .label = event.chord.name(),
                         .gap = event.chord.isNoChord(),
                         .colorIndex = 0,
                         .patternBars = 0};
    });
}

std::vector<LaneBlock> sectionBlocks(const nlohmann::json& data) { return blocks<SectionEvent>(data, sectionBlock); }

std::vector<LaneBlock> playedSectionBlocks(const nlohmann::json& data, bool autoFill, core::Ticks bar) {
    const std::vector<SectionEvent> events = eventsOrNone<SectionEvent>(data);
    std::vector<LaneBlock> result;
    std::optional<SectionEvent> playingMain;
    const auto add = [&](const SectionEvent& event, core::Ticks at, core::Ticks end) {
        LaneBlock block = sectionBlock(event);
        block.at = at;
        block.end = end;
        result.push_back(std::move(block));
    };
    for (std::size_t index = 0; index < events.size(); ++index) {
        const SectionEvent& event = events[index];
        const core::Ticks end = index + 1 < events.size() ? events[index + 1].at : event.at + bar;
        const std::optional<SectionEvent> main = landing(event, playingMain, autoFill);
        if (!main) {
            add(event, event.at, end);
            const bool startsMain = event.kind == Section::Main || event.kind == Section::FillTo;
            playingMain =
                startsMain
                    ? std::optional{SectionEvent{
                          .at = event.at, .kind = Section::Main, .variation = event.variation, .bars = event.bars}}
                    : std::nullopt;
            continue;
        }
        const core::Ticks fillEnd = std::min(end, (event.at / bar + 1) * bar);
        add(event.kind == Section::Main
                ? SectionEvent{.at = event.at, .kind = Section::FillTo, .variation = event.variation, .bars = 0}
                : event,
            event.at, fillEnd);
        if (fillEnd < end) {
            add(*main, fillEnd, end);
        }
        playingMain = main;
    }
    return result;
}

bool drawsBlocks(std::string_view kind) { return kind == kChordKind || kind == kStyleKind; }

std::optional<std::vector<LaneBlock>> laneBlocks(std::string_view kind, const nlohmann::json& data, bool autoFill,
                                                 core::Ticks bar) {
    if (kind == kChordKind) {
        return chordBlocks(data);
    }
    if (kind == kStyleKind) {
        return playedSectionBlocks(data, autoFill, bar);
    }
    return std::nullopt;
}

}  // namespace emerick::yamaha
