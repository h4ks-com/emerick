#include "emerick/model/document.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <set>

namespace emerick::model {

const ClipId& clipId(const Clip& clip) {
    return std::visit([](const auto& value) -> const ClipId& { return value.id; }, clip);
}

bool clipMuted(const Clip& clip) {
    return std::visit([](const auto& value) { return value.muted; }, clip);
}

void setClipMuted(Clip& clip, bool muted) {
    std::visit([muted](auto& value) { value.muted = muted; }, clip);
}

Track* findTrack(Document& document, const TrackId& id) {
    const auto found = std::ranges::find(document.tracks, id, &Track::id);
    return found == document.tracks.end() ? nullptr : &*found;
}

const Track* findTrack(const Document& document, const TrackId& id) {
    const auto found = std::ranges::find(document.tracks, id, &Track::id);
    return found == document.tracks.end() ? nullptr : &*found;
}

std::optional<ClipRef> findClip(const Document& document, const ClipId& id) {
    for (const Track& track : document.tracks) {
        const auto found = std::ranges::find(track.clips, id, [](const Clip& clip) { return clipId(clip); });
        if (found != track.clips.end()) {
            return ClipRef{&track, &*found};
        }
    }
    return std::nullopt;
}

std::vector<TrackId> busesFedBy(const Track& track) {
    std::vector<TrackId> buses = track.sends | std::views::transform(&Send::bus) | std::ranges::to<std::vector>();
    if (!track.outputBus.empty()) {
        buses.push_back(track.outputBus);
    }
    return buses;
}

bool reaches(const Document& document, const TrackId& from, const TrackId& to) {
    std::vector<TrackId> pending{from};
    std::set<TrackId> seen;
    while (!pending.empty()) {
        const TrackId at = pending.back();
        pending.pop_back();
        if (at == to) {
            return true;
        }
        const Track* track = findTrack(document, at);
        if (track != nullptr && seen.insert(at).second) {
            std::ranges::copy(busesFedBy(*track), std::back_inserter(pending));
        }
    }
    return false;
}

const Track* trackWithInsert(const Document& document, const InsertId& id) {
    const auto found = std::ranges::find_if(
        document.tracks, [&](const Track& track) { return std::ranges::contains(track.inserts, id, &Insert::id); });
    return found == document.tracks.end() ? nullptr : &*found;
}

const Marker* findMarker(const Document& document, const MarkerId& id) {
    const auto found = std::ranges::find(document.markers, id, &Marker::id);
    return found == document.markers.end() ? nullptr : &*found;
}

}  // namespace emerick::model
