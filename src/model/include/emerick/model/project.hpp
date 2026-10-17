#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "emerick/model/command.hpp"

namespace emerick::model {

// Owns the document. Every edit is a Command, so undo, redo and change listeners share one path.
class Project {
public:
    using Listener = std::function<void(const Change&)>;

    explicit Project(Document document = {});

    [[nodiscard]] const Document& document() const { return document_; }
    [[nodiscard]] const Track* track(const TrackId& id) const;
    [[nodiscard]] std::optional<ClipRef> findClip(const ClipId& id) const;

    // The track goes at index, or at the end when index is missing or too large.
    void addTrack(Track track, std::optional<std::size_t> index = std::nullopt);
    bool removeTrack(const TrackId& id);
    bool moveTrack(const TrackId& id, std::size_t index);
    void setTempo(const core::TempoMap& tempo);
    void setClockPorts(std::vector<std::string> ports);
    // The one path for property and clip edits: we edit a copy of the track and store it as one undo step.
    bool editTrack(const TrackId& id, std::string name, const std::function<void(Track&)>& edit);

    bool undo();
    bool redo();
    [[nodiscard]] std::optional<std::string> undoName() const;
    [[nodiscard]] std::optional<std::string> redoName() const;

    // Replaces the whole document and clears the undo history.
    void reset(Document document);

    std::size_t addListener(Listener listener);
    void removeListener(std::size_t handle);

private:
    void execute(Command command);
    void notify(const Change& change) const;

    Document document_;
    std::vector<Command> history_;
    std::size_t next_ = 0;  // history_[next_ - 1] is the last applied command
    std::map<std::size_t, Listener> listeners_;
    std::size_t nextHandle_ = 1;
};

}  // namespace emerick::model
