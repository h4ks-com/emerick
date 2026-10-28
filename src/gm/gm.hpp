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

#include "gm_sounds.hpp"

namespace emerick::gm {

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
    // Each entry is the General MIDI note minus the incoming note, so zero plays the note unchanged and a sum below
    // zero drops it. The main thread writes them on state load and the audio thread reads them.
    std::array<std::atomic<int>, kNotes> drumNoteOffsets{};
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
// The note the synth plays for a note on the channel, which the drum map changes on a drum channel, or -1 to drop it.
int drumNote(const Gm& gm, int channel, int note);

// Sends the Sound and Drum kit params to the synth as program changes.
void sendPrograms(Gm& gm);

extern const clap_plugin_state kState;
extern const clap_plugin_params kParams;

}  // namespace emerick::gm
