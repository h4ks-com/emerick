#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/engine/audio_backend.hpp"
#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

float gainFromDb(double db);

inline constexpr const char* kPanLawSetting = "audio.panLaw";

struct NamedPanLaw {
    engine::PanLaw law;
    const char* name;
};

inline constexpr std::array kPanLaws{
    NamedPanLaw{engine::PanLaw::compensated, "-3 dB compensated"}, NamedPanLaw{engine::PanLaw::zeroDb, "0 dB"},
    NamedPanLaw{engine::PanLaw::constantPower, "-3 dB constant power"},
    NamedPanLaw{engine::PanLaw::compromise, "-4.5 dB"}, NamedPanLaw{engine::PanLaw::linear, "-6 dB linear"}};

// A registry without the setting plays the default law.
engine::PanLaw panLawFromSettings(const Registry& registry);

// The building blocks a TrackType's toEngine starts from.
engine::Track audioEngineTrack(const model::Track& track, const std::filesystem::path& folder);
engine::Track midiEngineTrack(const model::Track& track);
engine::Track busEngineTrack(const model::Track& track);
// A MIDI track with an audio output, sends and inserts; the app attaches the instrument itself.
engine::Track instrumentEngineTrack(const model::Track& track);

// Each track's TrackType::toEngine result while the track, the tempo and the folder stay the same. A settings change
// must clear it, since toEngine may read settings.
struct EngineTrackCache {
    struct Entry {
        model::Track source;
        engine::Track converted;
    };
    core::TempoMap tempo;
    std::filesystem::path folder;
    std::map<std::string, Entry> tracks;
};

// Each track plays through its TrackType; a kind without one stays silent. Relative clip files resolve against the
// project folder, and takes go to <folder>/audio. With a cache, only the tracks that changed since the last call
// convert again.
engine::Session toSession(const model::Document& document, const Registry& registry,
                          const std::filesystem::path& folder, EngineTrackCache& cache);
engine::Session toSession(const model::Document& document, const Registry& registry,
                          const std::filesystem::path& folder);

// One track routed to a device that is gone or cannot run at sampleRate must not stop the others, so we clear its
// route for the engine and report it.
std::vector<engine::SessionError> dropUnusableDevices(engine::Session& session,
                                                      std::span<const engine::AudioDeviceInfo> devices,
                                                      double sampleRate);
// Older builds could store a device id with text after it; a known id that starts the stored one replaces it.
void repairDeviceIds(model::Document& document, std::span<const engine::AudioDeviceInfo> devices);

}  // namespace emerick::app
