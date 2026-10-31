// The instrument's main-thread side of its voices: reading the store, decoding a voice's waves into a bank the audio
// thread plays, and picking the voice. A store read runs on a thread of its own because a large store takes seconds;
// the thread hands the store over under its mutex and asks each waiting instance's host for a main-thread callback.

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <ranges>
#include <thread>

#include "voices.hpp"

namespace emerick::voices {

struct StoreLoad {
    std::filesystem::path folder;
    std::mutex mutex;
    // The hosts of the instances that wait for the store, and the store once the read finished.
    std::vector<const clap_host*> waiting;
    std::shared_ptr<const Store> store;
    // We declare the thread last so it joins before the members it uses are destroyed.
    std::jthread thread;
};

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

// What the instances in a process share for one folder: its store while any of them uses it, and its read while any
// of them waits for it. Only the host's main thread touches these.
struct FolderStore {
    std::weak_ptr<const Store> store;
    std::weak_ptr<StoreLoad> load;
};

std::map<std::filesystem::path, FolderStore>& folderStores() {
    static std::map<std::filesystem::path, FolderStore> stores;
    return stores;
}

std::shared_ptr<StoreLoad> startLoad(const std::filesystem::path& folder) {
    auto load = std::make_shared<StoreLoad>();
    load->folder = folder;
    // NOLINTNEXTLINE(bugprone-exception-escape): running out of memory while reading a store ends the process.
    load->thread = std::jthread([&state = *load, folder] {
        std::shared_ptr<const Store> store;
        try {
            store = std::make_shared<const Store>(loadStore(folder));
        } catch (const std::filesystem::filesystem_error&) {
            store = std::make_shared<const Store>();
        }
        const std::scoped_lock lock(state.mutex);
        state.store = std::move(store);
        for (const clap_host* host : state.waiting) {
            host->request_callback(host);
        }
    });
    return load;
}

std::shared_ptr<const Store> finishedStore(StoreLoad& load) {
    const std::scoped_lock lock(load.mutex);
    return load.store;
}

bool voiceExists(const Plugin& plugin, int voice) {
    return voice >= 0 && static_cast<std::size_t>(voice) < plugin.store->voices.size();
}

std::filesystem::path stateFolder(const Plugin& plugin) {
    return plugin.folder.empty() ? defaultStoreFolder() : std::filesystem::path{plugin.folder};
}

void rescanParams(const Plugin& plugin, clap_param_rescan_flags flags) {
    const auto* params =
        static_cast<const clap_host_params*>(plugin.host->get_extension(plugin.host, std::data(CLAP_EXT_PARAMS)));
    if (params != nullptr) {
        params->rescan(plugin.host, flags);
    }
}

std::string noteRange(int first, int last) {
    return first == last ? yamahaNoteName(first) : std::format("{} to {}", yamahaNoteName(first), yamahaNoteName(last));
}

void publishStore(Plugin& plugin, std::shared_ptr<const Store> store, const std::filesystem::path& folder) {
    const int selected = plugin.selected.load(std::memory_order_relaxed);
    if (voiceExists(plugin, selected)) {
        plugin.missingVoice = keyOf(plugin.store->voices[static_cast<std::size_t>(selected)]);
    }
    plugin.ownedBanks.clear();
    plugin.waves.clear();
    plugin.store = std::move(store);
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
    plugin.selected.store(-1, std::memory_order_relaxed);
    pickStateVoice(plugin);
    rescanParams(plugin, CLAP_PARAM_RESCAN_ALL);
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

void dropStoreLoad(Plugin& plugin) {
    if (!plugin.storeLoad) {
        return;
    }
    {
        const std::scoped_lock lock(plugin.storeLoad->mutex);
        std::erase(plugin.storeLoad->waiting, plugin.host);
    }
    plugin.storeLoad.reset();
}

void reloadStore(Plugin& plugin) {
    const std::filesystem::path folder = stateFolder(plugin);
    if (plugin.active || (plugin.storeLoaded && folder == plugin.loadedFolder) ||
        (plugin.storeLoad && plugin.storeLoad->folder == folder)) {
        return;
    }
    dropStoreLoad(plugin);
    FolderStore& shared = folderStores()[folder];
    if (std::shared_ptr<const Store> store = shared.store.lock()) {
        publishStore(plugin, std::move(store), folder);
        return;
    }
    plugin.storeLoad = shared.load.lock();
    if (!plugin.storeLoad) {
        plugin.storeLoad = startLoad(folder);
        shared.load = plugin.storeLoad;
    }
    const std::scoped_lock lock(plugin.storeLoad->mutex);
    plugin.storeLoad->waiting.push_back(plugin.host);
    if (plugin.storeLoad->store) {
        plugin.host->request_callback(plugin.host);
    }
}

void publishLoadedStore(Plugin& plugin) {
    if (!plugin.storeLoad) {
        return;
    }
    if (plugin.storeLoad->folder != stateFolder(plugin)) {
        dropStoreLoad(plugin);
        return;
    }
    std::shared_ptr<const Store> store = finishedStore(*plugin.storeLoad);
    if (!store) {
        return;
    }
    // The audio thread reads the banks and addresses we replace, so an active plugin publishes on its restart.
    if (plugin.active) {
        plugin.host->request_restart(plugin.host);
        return;
    }
    const std::filesystem::path folder = plugin.storeLoad->folder;
    dropStoreLoad(plugin);
    folderStores()[folder].store = store;
    publishStore(plugin, std::move(store), folder);
}

void pickStateVoice(Plugin& plugin) {
    const int found = plugin.missingVoice ? findVoice(*plugin.store, *plugin.missingVoice) : -1;
    if (found >= 0) {
        plugin.selected.store(found, std::memory_order_relaxed);
        plugin.missingVoice.reset();
    }
    buildBank(plugin, plugin.selected.load(std::memory_order_relaxed));
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
    rescanParams(plugin, CLAP_PARAM_RESCAN_TEXT);
}

std::string noiseWarning(const Plugin& plugin) {
    const int voice = plugin.selected.load(std::memory_order_relaxed);
    const Bank* bank = voiceExists(plugin, voice)
                           ? plugin.banks[static_cast<std::size_t>(voice)].load(std::memory_order_acquire)
                           : nullptr;
    if (bank == nullptr) {
        return {};
    }
    std::vector<std::pair<int, int>> keys =
        bank->zones | std::views::filter([](const BankZone& zone) {
            return zone.left->noisy || (zone.right != nullptr && zone.right->noisy);
        }) |
        std::views::transform([](const BankZone& zone) { return std::pair{zone.zone->keyLo, zone.zone->keyHi}; }) |
        std::ranges::to<std::vector>();
    if (keys.empty()) {
        return {};
    }
    std::ranges::sort(keys);
    std::vector<std::pair<int, int>> merged;
    for (const auto& [first, last] : keys) {
        if (!merged.empty() && first <= merged.back().second + 1) {
            merged.back().second = std::max(merged.back().second, last);
        } else {
            merged.emplace_back(first, last);
        }
    }
    return std::format(
        "Choose another voice for {}, where this voice's samples decode with noise",
        merged | std::views::transform([](const auto& range) { return noteRange(range.first, range.second); }) |
            std::views::join_with(std::string{", "}) | std::ranges::to<std::string>());
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
