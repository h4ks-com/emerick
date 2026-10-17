#include "emerick/model/command.hpp"

#include <algorithm>

#include "emerick/core/overloaded.hpp"

namespace emerick::model {
namespace {

using core::Overloaded;

void insertTrack(Document& document, const Track& track, std::size_t index) {
    const auto position = static_cast<std::ptrdiff_t>(std::min(index, document.tracks.size()));
    document.tracks.insert(document.tracks.begin() + position, track);
}

void eraseTrack(Document& document, std::size_t index) {
    if (index < document.tracks.size()) {
        document.tracks.erase(document.tracks.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void replaceTrack(Document& document, const Track& track) {
    if (Track* existing = findTrack(document, track.id)) {
        *existing = track;
    }
}

void moveTrack(Document& document, std::size_t from, std::size_t to) {
    if (from >= document.tracks.size() || to >= document.tracks.size()) {
        return;
    }
    const auto first = document.tracks.begin();
    const auto source = first + static_cast<std::ptrdiff_t>(from);
    const auto target = first + static_cast<std::ptrdiff_t>(to);
    if (from < to) {
        std::rotate(source, source + 1, target + 1);
    } else {
        std::rotate(target, source, source + 1);
    }
}

}  // namespace

void apply(Document& document, const Command& command) {
    std::visit(Overloaded{[&](const AddTrack& op) { insertTrack(document, op.track, op.index); },
                          [&](const RemoveTrack& op) { eraseTrack(document, op.index); },
                          [&](const ReplaceTrack& op) { replaceTrack(document, op.after); },
                          [&](const MoveTrack& op) { moveTrack(document, op.from, op.to); },
                          [&](const SetTempo& op) { document.tempo = op.after; },
                          [&](const SetClockPorts& op) { document.clockPorts = op.after; }},
               command.operation);
}

void revert(Document& document, const Command& command) {
    std::visit(Overloaded{[&](const AddTrack& op) { eraseTrack(document, op.index); },
                          [&](const RemoveTrack& op) { insertTrack(document, op.track, op.index); },
                          [&](const ReplaceTrack& op) { replaceTrack(document, op.before); },
                          [&](const MoveTrack& op) { moveTrack(document, op.to, op.from); },
                          [&](const SetTempo& op) { document.tempo = op.before; },
                          [&](const SetClockPorts& op) { document.clockPorts = op.before; }},
               command.operation);
}

Change changeOf(const Command& command) {
    return std::visit(
        Overloaded{[](const ReplaceTrack& op) { return Change{op.after.id}; }, [](const AddTrack&) { return Change{}; },
                   [](const RemoveTrack&) { return Change{}; }, [](const MoveTrack&) { return Change{}; },
                   [](const SetTempo&) { return Change{}; }, [](const SetClockPorts&) { return Change{}; }},
        command.operation);
}

}  // namespace emerick::model
