// The instrument's main-thread side of its voices: reading the store, decoding a voice's waves into a bank the audio
// thread plays, and picking the voice.

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <map>
#include <ranges>

#include "voices.hpp"

namespace emerick::voices {

namespace {

std::vector<std::string> labelsOf(const Store& store) {
    const auto plain = [](const Voice& voice) { return voice.pack + " / " + voice.name; };
    std::unordered_map<std::string, int> uses;
    for (const Voice& voice : store.voices) {
        ++uses[plain(voice)];
    }
    // We tell voices with the same pack and name apart by their bank and program, numbered from 1 as Yamaha lists them.
    return store.voices | std::views::transform([&](const Voice& voice) {
               const std::string label = plain(voice);
               return uses[label] == 1 ? label
                                       : std::format("{} ({}/{}/{})", label, voice.address.msb, voice.address.lsb,
                                                     voice.address.program + 1);
           }) |
           std::ranges::to<std::vector>();
}

const Wave* waveFor(Plugin& plugin, const std::string& name) {
    const auto cached = plugin.waves.find(name);
    if (cached != plugin.waves.end()) {
        return cached->second.get();
    }
    const auto path = plugin.store->waves.find(name);
    std::optional<Wave> wave = path != plugin.store->waves.end() ? loadWave(path->second) : std::nullopt;
    auto& slot = plugin.waves[name];
    if (wave) {
        slot = std::make_unique<Wave>(std::move(*wave));
    }
    return slot.get();
}

// Reading a store takes seconds, so the instances in a process share the store of a folder while any of them uses it.
// Only the host's main thread calls this.
std::shared_ptr<const Store> sharedStore(const std::filesystem::path& folder) {
    static std::map<std::filesystem::path, std::weak_ptr<const Store>> stores;
    std::weak_ptr<const Store>& slot = stores[folder];
    if (std::shared_ptr<const Store> store = slot.lock()) {
        return store;
    }
    auto store = std::make_shared<const Store>(loadStore(folder));
    slot = store;
    return store;
}

bool voiceExists(const Plugin& plugin, int voice) {
    return voice >= 0 && static_cast<std::size_t>(voice) < plugin.store->voices.size();
}

}  // namespace

VoiceKey keyOf(const Voice& voice) {
    return VoiceKey{.pack = voice.pack, .name = voice.name, .address = voice.address};
}

int findVoice(const Store& store, const VoiceKey& key) {
    const bool unnamed = key.pack.empty() && key.name.empty();
    const std::array<std::function<bool(const Voice&)>, 4> matches{
        [&](const Voice& voice) {
            return voice.pack == key.pack && voice.name == key.name && voice.address == key.address;
        },
        [&](const Voice& voice) { return voice.name == key.name && voice.address == key.address; },
        [&](const Voice& voice) { return voice.pack == key.pack && voice.name == key.name; },
        [&](const Voice& voice) { return unnamed && voice.address == key.address; }};
    for (const auto& match : matches) {
        const auto found = std::ranges::find_if(store.voices, match);
        if (found != store.voices.end()) {
            return static_cast<int>(std::distance(store.voices.begin(), found));
        }
    }
    return -1;
}

void reloadStore(Plugin& plugin) {
    const std::filesystem::path folder =
        plugin.folder.empty() ? defaultStoreFolder() : std::filesystem::path{plugin.folder};
    if (plugin.active || (plugin.storeLoaded && folder == plugin.loadedFolder)) {
        return;
    }
    const int selected = plugin.selected.load(std::memory_order_relaxed);
    if (voiceExists(plugin, selected)) {
        plugin.missingVoice = keyOf(plugin.store->voices[static_cast<std::size_t>(selected)]);
    }
    plugin.ownedBanks.clear();
    plugin.waves.clear();
    plugin.store = sharedStore(folder);
    plugin.loadedFolder = folder;
    plugin.storeLoaded = true;
    plugin.banks = std::vector<std::atomic<const Bank*>>(plugin.store->voices.size());
    plugin.labels = labelsOf(*plugin.store);
    plugin.addresses = std::views::iota(std::size_t{0}, plugin.store->voices.size()) |
                       std::views::transform([&](std::size_t voice) {
                           return std::pair{plugin.store->voices[voice].address, static_cast<int>(voice)};
                       }) |
                       std::ranges::to<std::vector>();
    std::ranges::stable_sort(plugin.addresses, {}, &std::pair<VoiceAddress, int>::first);
    const int found = plugin.missingVoice ? findVoice(*plugin.store, *plugin.missingVoice) : -1;
    plugin.selected.store(found, std::memory_order_relaxed);
    if (found >= 0) {
        plugin.missingVoice.reset();
    }
}

void buildBank(Plugin& plugin, int voice) {
    if (!voiceExists(plugin, voice) || plugin.banks[static_cast<std::size_t>(voice)].load() != nullptr) {
        return;
    }
    auto bank = std::make_unique<Bank>();
    for (const Zone& zone : plugin.store->voices[static_cast<std::size_t>(voice)].zones) {
        const Wave* left = waveFor(plugin, zone.leftWave);
        const Wave* right = zone.rightWave.empty() ? nullptr : waveFor(plugin, zone.rightWave);
        if (left != nullptr && (right != nullptr || zone.rightWave.empty())) {
            bank->zones.push_back(BankZone{.zone = &zone, .left = left, .right = right});
        }
    }
    plugin.banks[static_cast<std::size_t>(voice)].store(bank.get(), std::memory_order_release);
    plugin.ownedBanks.push_back(std::move(bank));
}

void selectVoice(Plugin& plugin, int voice, bool audioThread) {
    const int picked = voiceExists(plugin, voice) ? voice : -1;
    plugin.selected.store(picked, std::memory_order_relaxed);
    if (audioThread) {
        if (picked >= 0 && plugin.banks[static_cast<std::size_t>(picked)].load(std::memory_order_acquire) == nullptr) {
            plugin.bankWanted.store(picked, std::memory_order_relaxed);
            plugin.host->request_callback(plugin.host);
        }
        return;
    }
    plugin.missingVoice.reset();
    buildBank(plugin, picked);
}

}  // namespace emerick::voices
