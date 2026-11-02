#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "emerick/model/document.hpp"

namespace emerick::model {

// A set track means that track changed; no track means the track list, the tempo or the markers changed.
struct Change {
    std::optional<TrackId> track;
};

struct AddTrack {
    Track track;
    std::size_t index = 0;
};

struct RemoveTrack {
    Track track;
    std::size_t index = 0;
};

// Every property and clip edit stores the whole track before and after.
struct ReplaceTrack {
    Track before;
    Track after;
};

struct MoveTrack {
    std::size_t from;
    std::size_t to;
};

struct SetTempo {
    core::TempoMap before;
    core::TempoMap after;
};

struct SetClockPorts {
    std::vector<std::string> before;
    std::vector<std::string> after;
};

struct SetPluginData {
    std::string plugin;
    nlohmann::json before;
    nlohmann::json after;
};

struct SetMarkers {
    std::vector<Marker> before;
    std::vector<Marker> after;
};

struct SetCycle {
    Cycle before;
    Cycle after;
};

struct SetSelfContained {
    bool before = false;
    bool after = false;
};

struct Command;

// Several commands that undo and redo as one step.
struct Group {
    std::vector<Command> commands;
};

struct Command {
    std::string name;
    std::variant<AddTrack, RemoveTrack, ReplaceTrack, MoveTrack, SetTempo, SetClockPorts, SetPluginData, SetMarkers,
                 SetCycle, SetSelfContained, Group>
        operation;
};

void apply(Document& document, const Command& command);
void revert(Document& document, const Command& command);
Change changeOf(const Command& command);
// One track when both changes name the same track, else the whole list.
Change merged(const Change& first, const Change& second);

}  // namespace emerick::model
