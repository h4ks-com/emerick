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
#include "voices_params.hpp"
#include "wave.hpp"

namespace emerick::voices {

inline constexpr float kDefaultAttack = 0.002F;
inline constexpr float kDefaultRelease = 0.3F;

// Names a voice in the state by what it is, so a store that gained or lost voices keeps the selection.
struct VoiceKey {
    std::string pack;
    std::string name;
    VoiceAddress address;
};

// A channel's controllers at their General MIDI defaults; 127 in both RPN bytes selects no parameter.
struct ChannelControls {
    int volume = 100;
    int expression = 127;
    int pan = 64;
    int bend = 8192;
    int bendSemitones = 2;
    int bendCents = 0;
    int rpnMsb = 127;
    int rpnLsb = 127;
};

// A voice's banks: one where each noisy zone plays the nearest clean zone of its element, and one as decoded.
struct VoiceBanks {
    std::atomic<const Bank*> clean{nullptr};
    std::atomic<const Bank*> decoded{nullptr};
};

// A store read on its own thread, shared by the instances that wait for the same folder.
struct StoreLoad;

struct Plugin {
    clap_plugin plugin{};
    const clap_host* host = nullptr;
    // The store folder the state names; empty reads the default store.
    std::string folder;
    std::filesystem::path loadedFolder;
    bool storeLoaded = false;
    // The read of the folder the state names, until the main thread publishes its store.
    std::shared_ptr<StoreLoad> storeLoad;
    // Shared by every instance that reads the same folder; the banks point into it.
    std::shared_ptr<const Store> store = std::make_shared<const Store>();
    // "Pack / Voice" for each voice; the Voice param counts None as 0 and the voices from 1.
    std::vector<std::string> labels;
    std::vector<std::pair<VoiceAddress, int>> addresses;
    std::unordered_map<std::string, std::unique_ptr<Wave>> waves;
    std::vector<std::unique_ptr<Bank>> ownedBanks;
    // The main thread builds a voice's bank once and publishes it here, and the banks live until the store reloads.
    std::vector<VoiceBanks> banks;
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
    std::atomic<bool> playNoisyAsDecoded{false};
    bool active = false;
    double sampleRate = 48000.0;
    // Audio thread only.
    Sampler sampler;
    std::array<int, kChannels> bankMsb{};
    std::array<int, kChannels> bankLsb{};
    std::array<ChannelControls, kChannels> controls{};
    // The bank that played last, which keeps sounding while a program change waits for its new bank.
    const Bank* playingBank = nullptr;
    bool voiceChanged = false;
};

inline Plugin& pluginOf(const clap_plugin* plugin) { return *static_cast<Plugin*>(plugin->plugin_data); }

// CLAP hands every event as its header, and the type in that header says which struct it starts.
template <typename Event>
const Event& eventAs(const clap_event_header* header) {
    return *reinterpret_cast<const Event*>(header);
}

VoiceKey keyOf(const Voice& voice);
// The voice with this pack, name and address, else with this name and address, else with this pack and name, else
// with this address for a key that names no voice; -1 when the store holds none.
int findVoice(const Store& store, const VoiceKey& key);

// Main thread: starts reading the store the state names on a thread of its own, unless it is loaded or loading already,
// and publishes a store another instance holds right away. We read it on first use, so a state that names another
// folder never reads the default store.
void reloadStore(Plugin& plugin);
// Main thread: publishes the store once its read finished and the plugin is inactive, and asks the host to restart an
// active plugin so it can. The host then rescans the params, and the voice the state names gets its bank.
void publishLoadedStore(Plugin& plugin);
// Main thread: stops waiting for the store, before the plugin is destroyed or reads another folder.
void dropStoreLoad(Plugin& plugin);
// Main thread: picks the voice the state names once the store holds it and builds its bank.
void pickStateVoice(Plugin& plugin);
// Main thread: decodes the voice's waves once and publishes its bank to the audio thread.
void buildBank(Plugin& plugin, int voice);
// The voice's bank that the Noisy samples param picks, or null before the main thread builds it.
const Bank* playableBank(const Plugin& plugin, int voice);
// Main thread: the warning that names the notes of the picked voice whose waves decode with noise, or empty.
std::string noiseWarning(const Plugin& plugin);
// Picks a voice, or none for -1. On the audio thread it asks the main thread to build the bank.
void selectVoice(Plugin& plugin, int voice, bool audioThread);
// Stores a param change; Voice changes on the audio thread leave the bank to the main thread.
void setParam(Plugin& plugin, const clap_event_param_value& change, bool audioThread);

extern const clap_plugin_state kState;
extern const clap_plugin_params kParams;

}  // namespace emerick::voices
