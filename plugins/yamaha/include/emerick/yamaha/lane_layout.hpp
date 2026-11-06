#pragma once

#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/core/time.hpp"

namespace emerick::yamaha {

inline constexpr const char* kChordKind = "yamaha.chord";
inline constexpr const char* kStyleKind = "yamaha.style";

// The last block of a lane lasts until the next event, so the lane draws it and selects it as one bar.
inline constexpr core::Ticks kLastBlockTicks = 4 * core::kTicksPerQuarter;

// A Chords or Style track keeps its events in data as a JSON array; a new track has no data and no events. Damaged
// data throws nlohmann::json::exception or std::invalid_argument.
template <class Event>
std::vector<Event> sortedEvents(const nlohmann::json& data) {
    if (data.is_null()) {
        return {};
    }
    auto events = data.get<std::vector<Event>>();
    std::ranges::stable_sort(events, {}, &Event::at);
    return events;
}

// One event of a Chords or Style lane as the lane draws it, up to the next event. A gap is a No Chord, colorIndex
// indexes the theme's track colors, and patternBars is how often a Main's pattern repeats.
struct LaneBlock {
    core::Ticks at = 0;
    core::Ticks end = 0;
    std::string label;
    bool gap = false;
    int colorIndex = 0;
    int patternBars = 0;
};

void to_json(nlohmann::json& json, const LaneBlock& block);

// The blocks of a Chords or Style track's data, or none when the data is damaged.
std::vector<LaneBlock> chordBlocks(const nlohmann::json& data);
std::vector<LaneBlock> sectionBlocks(const nlohmann::json& data);
// The sections of a Style track's data as the Arranger plays them: a fill or a break plays to the end of its bar and
// then the Main it lands on, and with Auto fill a change from a Main to a Main starts with the new Main's fill.
std::vector<LaneBlock> playedSectionBlocks(const nlohmann::json& data, bool autoFill, core::Ticks bar);
// A Chords or Style track draws its events as blocks; every other kind draws its clips.
bool drawsBlocks(std::string_view kind);
// The blocks of a track of the kind as the song plays them, or nullopt for a kind that draws clips.
std::optional<std::vector<LaneBlock>> laneBlocks(std::string_view kind, const nlohmann::json& data, bool autoFill,
                                                 core::Ticks bar);

}  // namespace emerick::yamaha
