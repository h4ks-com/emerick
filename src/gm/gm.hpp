#pragma once

#include <clap/clap.h>
#include <fluidsynth.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace emerick::gm {

inline constexpr clap_id kSoundParam = 0;
inline constexpr clap_id kDrumKitParam = 1;
inline constexpr clap_id kFollowParam = 2;
inline constexpr clap_id kHighestNoteParam = 3;
inline constexpr int kHighestNote = 127;
inline constexpr int kChannels = 16;

inline std::filesystem::path& bundledSoundFont() {
    static std::filesystem::path path;
    return path;
}

struct SettingsDeleter {
    void operator()(fluid_settings_t* settings) const { delete_fluid_settings(settings); }
};

struct SynthDeleter {
    void operator()(fluid_synth_t* synth) const { delete_fluid_synth(synth); }
};

struct Gm {
    clap_plugin plugin{};
    const clap_host* host = nullptr;
    std::string soundFont;
    // The main thread writes these and the audio thread sends them to the synth as program changes while pending.
    std::atomic<int> sound{0};
    std::atomic<int> drumKit{0};
    std::atomic<bool> followProgramChanges{true};
    std::atomic<int> highestNote{kHighestNote};
    std::atomic<bool> programsPending{true};
    bool soundChanged = false;
    bool drumKitChanged = false;
    std::array<bool, kChannels> drumChannels{};
    std::filesystem::path loadedSoundFont;
    double loadedRate = 0.0;
    bool active = false;
    // The synth reads its settings until it is deleted, so we declare the settings first and they outlive it.
    std::unique_ptr<fluid_settings_t, SettingsDeleter> settings;
    std::unique_ptr<fluid_synth_t, SynthDeleter> synth;
};

inline Gm& gmOf(const clap_plugin* plugin) { return *static_cast<Gm*>(plugin->plugin_data); }

inline std::filesystem::path wantedSoundFont(const Gm& gm) {
    return gm.soundFont.empty() ? bundledSoundFont() : std::filesystem::path{gm.soundFont};
}

// CLAP hands every event as its header, and the type in that header says which struct it starts.
template <typename Event>
const Event& eventAs(const clap_event_header* header) {
    return *reinterpret_cast<const Event*>(header);
}

inline int clampedIndex(double value, std::size_t count) {
    return std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(count) - 1);
}

// Stores a Sound, Drum kit, Follow program changes or Highest note change for the audio thread.
void setParam(Gm& gm, const clap_event_param_value& change);

void playMidi(Gm& gm, const std::array<std::uint8_t, 3>& data);

// Sends the Sound and Drum kit params to the synth as program changes.
void sendPrograms(Gm& gm);

extern const clap_plugin_state kState;
extern const clap_plugin_params kParams;

}  // namespace emerick::gm
