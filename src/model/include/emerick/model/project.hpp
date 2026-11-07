#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "emerick/model/command.hpp"

namespace emerick::model {

// Each step can hold whole copies of a track, so we keep only the latest steps.
inline constexpr std::size_t kMaxUndoSteps = 100;

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
    // A null value removes the plugin's data.
    void setPluginData(const std::string& plugin, nlohmann::json value, std::string name);
    // Stores the markers sorted by position.
    void setMarkers(std::vector<Marker> markers, std::string name);
    void setCycle(const Cycle& cycle, std::string name);
    void setSelfContained(bool selfContained, std::string name);
    // Every edit made inside edits becomes one undo step named name, and listeners hear one change at the end.
    void group(std::string name, const std::function<void()>& edits);
    // When edits throw, we revert the edits they made and restore the redo steps, so the document and the history
    // are as they were before.
    void atomic(const std::function<void()>& edits);
    // The one path for property and clip edits: we edit a copy of the track and store it as one undo step.
    bool editTrack(const TrackId& id, std::string name, const std::function<void(Track&)>& edit);
    // A gesture such as a drag is one undo step that each update replaces: we revert the last update's edits and run
    // the new ones, so the document follows the drag. Any other edit, undo or redo ends the gesture first.
    void updateGesture(std::string name, const std::function<void()>& edits);
    // True while a gesture update runs its edits on the document it just reverted.
    [[nodiscard]] bool updatingGesture() const { return gestureDepth_ > 0; }
    // Keeps the gesture as one undo step and returns true, or drops it when the document is back where it started.
    bool endGesture();
    void cancelGesture();

    bool undo();
    bool redo();
    [[nodiscard]] std::optional<std::string> undoName() const;
    [[nodiscard]] std::optional<std::string> redoName() const;

    // Replaces the whole document and clears the undo history.
    void reset(Document document);

    std::size_t addListener(Listener listener);
    void removeListener(std::size_t handle);

private:
    class DepthScope {
    public:
        explicit DepthScope(int& depth);
        ~DepthScope();
        DepthScope(const DepthScope&) = delete;
        DepthScope& operator=(const DepthScope&) = delete;
        DepthScope(DepthScope&&) = delete;
        DepthScope& operator=(DepthScope&&) = delete;

    private:
        int& depth_;
    };

    struct Checkpoint {
        std::size_t first;
        std::vector<Command> redo;
        bool gestureStep;
        std::size_t resets;
    };

    // Reverts the edits made since the checkpoint; after a reset or an undo past it there is nothing to revert to.
    void rollBack(Checkpoint checkpoint);
    void execute(Command command);
    void trimHistory();
    // Folds the commands from first on into one step named name.
    void collapseSince(std::size_t first, std::string name);
    std::optional<Change> revertGesture();
    void leaveGesture();
    void notify(const Change& change) const;

    Document document_;
    std::vector<Command> history_;
    std::size_t next_ = 0;  // history_[next_ - 1] is the last applied command
    std::map<std::size_t, Listener> listeners_;
    std::size_t nextHandle_ = 1;
    int groupDepth_ = 0;
    // Checkpoints and groups hold positions in history_, so we trim it only when none is open.
    int editDepth_ = 0;
    bool groupChanged_ = false;
    // history_[next_ - 1] is the open gesture's step.
    bool gestureStep_ = false;
    int gestureDepth_ = 0;
    std::size_t resets_ = 0;
};

}  // namespace emerick::model
