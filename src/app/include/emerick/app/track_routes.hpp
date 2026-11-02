#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "emerick/model/document.hpp"

namespace emerick::app {

// Audio routes an input and an output and holds audio clips; bus routes only an output, holds no clips and takes
// sends. MIDI holds MIDI clips and plays them to a MIDI output, and through the track's instrument when it has one.
struct RouteSections {
    bool audio = false;
    bool midi = false;
    bool bus = false;
};

inline constexpr RouteSections kAudioRoutes{.audio = true};
inline constexpr RouteSections kMidiRoutes{.midi = true};
inline constexpr RouteSections kBusRoutes{.bus = true};

struct CoreTrackKind {
    std::string_view kind;
    RouteSections routes;
};

// The track kinds the app itself registers; plugins register the others.
inline constexpr std::array kCoreTrackKinds{
    CoreTrackKind{"audio", kAudioRoutes},
    CoreTrackKind{"midi", kMidiRoutes},
    CoreTrackKind{"bus", kBusRoutes},
};

std::optional<RouteSections> coreRoutes(std::string_view kind);

// Tracks that play audio have a fader, pan, inserts, sends and an audio output: audio tracks, buses, and MIDI tracks
// while they hold an instrument.
[[nodiscard]] inline bool playsAudio(const RouteSections& routes, const model::Track& track) {
    return routes.audio || routes.bus || (routes.midi && track.instrument.has_value());
}

}  // namespace emerick::app
