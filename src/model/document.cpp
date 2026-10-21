#include "emerick/model/document.hpp"

#include <algorithm>

namespace emerick::model {

const ClipId& clipId(const Clip& clip) {
    return std::visit([](const auto& value) -> const ClipId& { return value.id; }, clip);
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

const Marker* findMarker(const Document& document, const MarkerId& id) {
    const auto found = std::ranges::find(document.markers, id, &Marker::id);
    return found == document.markers.end() ? nullptr : &*found;
}

}  // namespace emerick::model
