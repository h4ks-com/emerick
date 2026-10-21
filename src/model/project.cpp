#include "emerick/model/project.hpp"

#include <algorithm>
#include <utility>

namespace emerick::model {

Project::Project(Document document) : document_(std::move(document)) {}

const Track* Project::track(const TrackId& id) const { return findTrack(document_, id); }

std::optional<ClipRef> Project::findClip(const ClipId& id) const { return model::findClip(document_, id); }

void Project::addTrack(Track track, std::optional<std::size_t> index) {
    const std::size_t at = std::min(index.value_or(document_.tracks.size()), document_.tracks.size());
    auto name = "add track " + track.name;
    execute({std::move(name), AddTrack{std::move(track), at}});
}

bool Project::removeTrack(const TrackId& id) {
    const auto found = std::ranges::find(document_.tracks, id, &Track::id);
    if (found == document_.tracks.end()) {
        return false;
    }
    const auto index = static_cast<std::size_t>(found - document_.tracks.begin());
    execute({"remove track " + found->name, RemoveTrack{*found, index}});
    return true;
}

bool Project::moveTrack(const TrackId& id, std::size_t index) {
    const auto found = std::ranges::find(document_.tracks, id, &Track::id);
    if (found == document_.tracks.end() || index >= document_.tracks.size()) {
        return false;
    }
    const auto from = static_cast<std::size_t>(found - document_.tracks.begin());
    execute({"move track " + found->name, MoveTrack{from, index}});
    return true;
}

void Project::setTempo(const core::TempoMap& tempo) { execute({"set tempo", SetTempo{document_.tempo, tempo}}); }

void Project::setClockPorts(std::vector<std::string> ports) {
    execute({"set clock out", SetClockPorts{document_.clockPorts, std::move(ports)}});
}

void Project::setPluginData(const std::string& plugin, nlohmann::json value, std::string name) {
    nlohmann::json before = document_.pluginData.contains(plugin) ? document_.pluginData.at(plugin) : nlohmann::json{};
    execute({std::move(name), SetPluginData{plugin, std::move(before), std::move(value)}});
}

void Project::setMarkers(std::vector<Marker> markers, std::string name) {
    std::ranges::stable_sort(markers, {}, &Marker::at);
    execute({std::move(name), SetMarkers{document_.markers, std::move(markers)}});
}

void Project::group(std::string name, const std::function<void()>& edits) {
    leaveGesture();
    const std::size_t first = next_;
    {
        const DepthScope scope(groupDepth_);
        edits();
    }
    if (next_ != first) {
        collapseSince(first, std::move(name));
    }
    if (groupDepth_ == 0 && std::exchange(groupChanged_, false)) {
        notify(Change{});
    }
}

void Project::collapseSince(std::size_t first, std::string name) {
    const auto begin = history_.begin() + static_cast<std::ptrdiff_t>(first);
    std::vector<Command> commands(std::make_move_iterator(begin), std::make_move_iterator(history_.end()));
    history_.erase(begin, history_.end());
    history_.push_back({std::move(name), Group{std::move(commands)}});
    next_ = history_.size();
}

void Project::updateGesture(std::string name, const std::function<void()>& edits) {
    const std::optional<Change> reverted = revertGesture();
    const std::size_t first = next_;
    {
        const DepthScope group(groupDepth_);
        const DepthScope gesture(gestureDepth_);
        edits();
    }
    groupChanged_ = false;
    if (next_ - first > 1) {
        collapseSince(first, std::move(name));
    }
    gestureStep_ = next_ != first;
    if (gestureStep_ && reverted) {
        notify(merged(*reverted, changeOf(history_[next_ - 1])));
    } else if (gestureStep_) {
        notify(changeOf(history_[next_ - 1]));
    } else if (reverted) {
        notify(*reverted);
    }
}

bool Project::endGesture() {
    if (!std::exchange(gestureStep_, false)) {
        return false;
    }
    Document before = document_;
    revert(before, history_[next_ - 1]);
    if (before != document_) {
        return true;
    }
    history_.pop_back();
    --next_;
    return false;
}

void Project::cancelGesture() {
    if (const std::optional<Change> reverted = revertGesture()) {
        notify(*reverted);
    }
}

std::optional<Change> Project::revertGesture() {
    if (!std::exchange(gestureStep_, false)) {
        return std::nullopt;
    }
    Change change = changeOf(history_[next_ - 1]);
    revert(document_, history_[next_ - 1]);
    history_.pop_back();
    --next_;
    return change;
}

void Project::leaveGesture() {
    if (gestureDepth_ == 0) {
        gestureStep_ = false;
    }
}

bool Project::editTrack(const TrackId& id, std::string name, const std::function<void(Track&)>& edit) {
    const Track* current = track(id);
    if (current == nullptr) {
        return false;
    }
    Track edited = *current;
    edit(edited);
    edited.id = id;
    execute({std::move(name), ReplaceTrack{*current, std::move(edited)}});
    return true;
}

bool Project::undo() {
    leaveGesture();
    if (next_ == 0) {
        return false;
    }
    const Command& command = history_[--next_];
    revert(document_, command);
    notify(changeOf(command));
    return true;
}

bool Project::redo() {
    leaveGesture();
    if (next_ == history_.size()) {
        return false;
    }
    const Command& command = history_[next_++];
    apply(document_, command);
    notify(changeOf(command));
    return true;
}

std::optional<std::string> Project::undoName() const {
    return next_ == 0 ? std::nullopt : std::optional{history_[next_ - 1].name};
}

std::optional<std::string> Project::redoName() const {
    return next_ == history_.size() ? std::nullopt : std::optional{history_[next_].name};
}

void Project::reset(Document document) {
    gestureStep_ = false;
    document_ = std::move(document);
    history_.clear();
    next_ = 0;
    notify(Change{});
}

std::size_t Project::addListener(Listener listener) {
    listeners_.emplace(nextHandle_, std::move(listener));
    return nextHandle_++;
}

void Project::removeListener(std::size_t handle) { listeners_.erase(handle); }

void Project::execute(Command command) {
    leaveGesture();
    apply(document_, command);
    history_.resize(next_);
    const Change change = changeOf(command);
    history_.push_back(std::move(command));
    ++next_;
    if (groupDepth_ > 0) {
        groupChanged_ = true;
        return;
    }
    // A group whose edits threw never notified, so we report its edits along with this one.
    notify(std::exchange(groupChanged_, false) ? Change{} : change);
}

Project::DepthScope::DepthScope(int& depth) : depth_(depth) { ++depth_; }

Project::DepthScope::~DepthScope() { --depth_; }

void Project::notify(const Change& change) const {
    const auto snapshot = listeners_;
    for (const auto& entry : snapshot) {
        entry.second(change);
    }
}

}  // namespace emerick::model
