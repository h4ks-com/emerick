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

template <class Event, class Describe>
std::vector<LaneBlock> blocks(const nlohmann::json& data, Describe describe) {
    std::vector<Event> events;
    try {
        events = sortedEvents<Event>(data);
    } catch (const nlohmann::json::exception&) {
        return {};
    } catch (const std::invalid_argument&) {
        return {};
    }
    std::vector<LaneBlock> result;
    for (std::size_t index = 0; index < events.size(); ++index) {
        LaneBlock block = describe(events[index]);
        block.at = events[index].at;
        block.end = index + 1 < events.size() ? events[index + 1].at : block.at + kLastBlockTicks;
        result.push_back(std::move(block));
    }
    return result;
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

std::vector<LaneBlock> sectionBlocks(const nlohmann::json& data) {
    return blocks<SectionEvent>(data, [](const SectionEvent& event) {
        const int base = kSectionColors.at(static_cast<std::size_t>(event.kind));
        return LaneBlock{.at = 0,
                         .end = 0,
                         .label = sectionName(event),
                         .gap = false,
                         .colorIndex = event.kind == Section::Main ? base + event.variation : base,
                         .patternBars = event.bars};
    });
}

bool drawsBlocks(std::string_view kind) { return kind == kChordKind || kind == kStyleKind; }

std::optional<std::vector<LaneBlock>> laneBlocks(std::string_view kind, const nlohmann::json& data) {
    if (kind == kChordKind) {
        return chordBlocks(data);
    }
    if (kind == kStyleKind) {
        return sectionBlocks(data);
    }
    return std::nullopt;
}

}  // namespace emerick::yamaha
