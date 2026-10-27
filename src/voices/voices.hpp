#pragma once

#include <clap/clap.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rt_sampler.hpp"
#include "store.hpp"
#include "wave.hpp"

namespace emerick::voices {

inline constexpr clap_id kVoiceParam = 0;
inline constexpr clap_id kVolumeParam = 1;
inline constexpr clap_id kTuneParam = 2;
inline constexpr clap_id kAttackParam = 3;
inline constexpr clap_id kReleaseParam = 4;
inline constexpr clap_id kFollowParam = 5;
inline constexpr std::uint32_t kParamCount = 6;

inline constexpr float kDefaultAttack = 0.002F;
inline constexpr float kDefaultRelease = 0.3F;

// Names a voice in the state by what it is, so a store that gained or lost voices keeps the selection.
struct VoiceKey {
    std::string pack;
    std::string name;
    VoiceAddress address;
};

struct Plugin {
    clap_plugin plugin{};
    const clap_host* host = nullptr;
    // The store folder the state names; empty reads the default store.
    std::string folder;
    std::filesystem::path loadedFolder;
    bool storeLoaded = false;
    // Shared by every instance that reads the same folder; the banks point into it.
    std::shared_ptr<const Store> store = std::make_shared<const Store>();
    // "Pack / Voice" for each voice; the Voice param counts None as 0 and the voices from 1.
    std::vector<std::string> labels;
    std::vector<std::pair<VoiceAddress, int>> addresses;
    std::unordered_map<std::string, std::unique_ptr<Wave>> waves;
    std::vector<std::unique_ptr<Bank>> ownedBanks;
    // The main thread builds a voice's bank once and publishes it here, and the banks live until the store reloads.
    std::vector<std::atomic<const Bank*>> banks;
    // A state voice the store does not hold, which we save again unless another voice is picked.
    std::optional<VoiceKey> missingVoice;
    std::atomic<int> selected{-1};
    // The voice the audio thread picked before its bank was built; the main thread builds it on its callback.
    std::atomic<int> bankWanted{-1};
    std::atomic<float> volumeDb{0.0F};
    std::atomic<float> tune{0.0F};
    std::atomic<float> attack{kDefaultAttack};
    std::atomic<float> release{kDefaultRelease};
    std::atomic<bool> followProgramChanges{false};
    bool active = false;
    double sampleRate = 48000.0;
    // Audio thread only.
    Sampler sampler;
    std::array<int, kChannels> bankMsb{};
    std::array<int, kChannels> bankLsb{};
    bool voiceChanged = false;
};

inline Plugin& pluginOf(const clap_plugin* plugin) { return *static_cast<Plugin*>(plugin->plugin_data); }

// CLAP hands every event as its header, and the type in that header says which struct it starts.
template <typename Event>
const Event& eventAs(const clap_event_header* header) {
    return *reinterpret_cast<const Event*>(header);
}

VoiceKey keyOf(const Voice& voice);
// The voice with this pack, name and address, else with this name and address, else with this pack and name; -1 when
// the store holds none.
int findVoice(const Store& store, const VoiceKey& key);

// Main thread: reads the store the state names unless it is loaded already or the plugin is active. We read it on
// first use, so a state that names another folder never reads the default store.
void reloadStore(Plugin& plugin);
// Main thread: decodes the voice's waves once and publishes its bank to the audio thread.
void buildBank(Plugin& plugin, int voice);
// Picks a voice, or none for -1. On the audio thread it asks the main thread to build the bank.
void selectVoice(Plugin& plugin, int voice, bool audioThread);
// Stores a param change; Voice changes on the audio thread leave the bank to the main thread.
void setParam(Plugin& plugin, const clap_event_param_value& change, bool audioThread);

extern const clap_plugin_state kState;
extern const clap_plugin_params kParams;

}  // namespace emerick::voices
