// The expansion voices instrument's state and params. Its state is JSON with the store folder, the voice by pack, name
// and address, and the other params.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <nlohmann/json.hpp>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

#include "voices.hpp"

namespace emerick::voices {

namespace {

using Json = nlohmann::json;

struct ParamRange {
    clap_id id;
    const char* name;
    const char* stateKey;
    double min;
    double max;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamRange, kParamCount - 1> kRanges{{
    {kVolumeParam, "Volume", "volume", -60.0, 12.0, 0.0, false},
    {kTuneParam, "Tune", "tune", -24.0, 24.0, 0.0, false},
    {kAttackParam, "Attack", "attack", 0.0, 5.0, kDefaultAttack, false},
    {kReleaseParam, "Release", "release", 0.0, 10.0, kDefaultRelease, false},
    {kFollowParam, "Follow program changes", "followProgramChanges", 0.0, 1.0, 0.0, true},
}};

constexpr std::array<std::string_view, 2> kSwitchLabels{"Off", "On"};

const ParamRange* rangeOf(clap_id id) {
    const auto* const found = std::ranges::find(kRanges, id, &ParamRange::id);
    return found != kRanges.end() ? &*found : nullptr;
}

std::atomic<float>* numberOf(Plugin& plugin, clap_id id) {
    switch (id) {
        case kVolumeParam:
            return &plugin.volumeDb;
        case kTuneParam:
            return &plugin.tune;
        case kAttackParam:
            return &plugin.attack;
        case kReleaseParam:
            return &plugin.release;
        default:
            return nullptr;
    }
}

int voiceIndex(const Plugin& plugin, double value) {
    return std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(plugin.labels.size())) - 1;
}

bool writeAll(const clap_ostream* stream, std::string_view text) {
    while (!text.empty()) {
        const std::int64_t count = stream->write(stream, text.data(), text.size());
        if (count <= 0) {
            return false;
        }
        text.remove_prefix(static_cast<std::size_t>(count));
    }
    return true;
}

Json voiceJson(const Plugin& plugin) {
    const int selected = plugin.selected.load(std::memory_order_relaxed);
    const std::optional<VoiceKey> key =
        selected >= 0 ? std::optional{keyOf(plugin.store->voices[static_cast<std::size_t>(selected)])}
                      : plugin.missingVoice;
    if (!key) {
        return nullptr;
    }
    return Json{{"pack", key->pack},
                {"name", key->name},
                {"msb", key->address.msb},
                {"lsb", key->address.lsb},
                {"program", key->address.program}};
}

bool saveState(const clap_plugin* plugin, const clap_ostream* stream) {
    Plugin& voices = pluginOf(plugin);
    Json state{{"folder", voices.folder},
               {"voice", voiceJson(voices)},
               {"followProgramChanges", voices.followProgramChanges.load(std::memory_order_relaxed)}};
    for (const ParamRange& range : kRanges) {
        if (const std::atomic<float>* value = numberOf(voices, range.id)) {
            state[range.stateKey] = value->load(std::memory_order_relaxed);
        }
    }
    return writeAll(stream, state.dump());
}

std::optional<VoiceKey> voiceKeyOf(const Json& voice) {
    if (!voice.is_object()) {
        return std::nullopt;
    }
    return VoiceKey{
        .pack = voice.value("pack", std::string{}),
        .name = voice.value("name", std::string{}),
        .address = {.msb = voice.value("msb", 0), .lsb = voice.value("lsb", 0), .program = voice.value("program", 0)}};
}

bool loadState(const clap_plugin* plugin, const clap_istream* stream) {
    std::string text;
    std::array<char, 4096> chunk{};
    for (;;) {
        const std::int64_t count = stream->read(stream, chunk.data(), chunk.size());
        if (count < 0) {
            return false;
        }
        if (count == 0) {
            break;
        }
        text.append(chunk.data(), static_cast<std::size_t>(count));
    }
    Json state = Json::parse(text, nullptr, false);
    if (!state.is_object()) {
        state = Json::object();
    }
    Plugin& voices = pluginOf(plugin);
    const auto number = [&](const char* key, float fallback) {
        const auto found = state.find(key);
        return found != state.end() && found->is_number() ? found->get<float>() : fallback;
    };
    for (const ParamRange& range : kRanges) {
        if (std::atomic<float>* value = numberOf(voices, range.id)) {
            value->store(std::clamp(number(range.stateKey, static_cast<float>(range.defaultValue)),
                                    static_cast<float>(range.min), static_cast<float>(range.max)),
                         std::memory_order_relaxed);
        }
    }
    voices.followProgramChanges.store(state.value("followProgramChanges", false), std::memory_order_relaxed);
    voices.folder = state.value("folder", std::string{});
    const std::filesystem::path folder =
        voices.folder.empty() ? defaultStoreFolder() : std::filesystem::path{voices.folder};
    voices.missingVoice = voiceKeyOf(state.value("voice", Json{}));
    voices.selected.store(-1, std::memory_order_relaxed);
    if (voices.active && folder != voices.loadedFolder) {
        voices.host->request_restart(voices.host);
        return true;
    }
    reloadStore(voices);
    if (!voices.storeLoad) {
        pickStateVoice(voices);
    }
    return true;
}

std::uint32_t paramCount(const clap_plugin* /*unused*/) { return kParamCount; }

bool paramInfo(const clap_plugin* plugin, std::uint32_t index, clap_param_info* info) {
    if (index >= kParamCount) {
        return false;
    }
    *info = {};
    info->id = index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (index == kVoiceParam) {
        info->flags |= CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
        Plugin& voices = pluginOf(plugin);
        reloadStore(voices);
        std::strncpy(std::data(info->name), "Voice", std::size(info->name));
        info->max_value = static_cast<double>(voices.labels.size());
        return true;
    }
    const ParamRange& range = *rangeOf(index);
    info->flags |= range.stepped ? CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM : 0U;
    std::strncpy(std::data(info->name), range.name, std::size(info->name));
    info->min_value = range.min;
    info->max_value = range.max;
    info->default_value = range.defaultValue;
    return true;
}

bool paramValue(const clap_plugin* plugin, clap_id id, double* value) {
    Plugin& voices = pluginOf(plugin);
    if (id == kVoiceParam) {
        *value = voices.selected.load(std::memory_order_relaxed) + 1;
    } else if (id == kFollowParam) {
        *value = voices.followProgramChanges.load(std::memory_order_relaxed) ? 1.0 : 0.0;
    } else if (std::atomic<float>* number = numberOf(voices, id)) {
        *value = number->load(std::memory_order_relaxed);
    } else {
        return false;
    }
    return true;
}

std::string paramLabel(const Plugin& plugin, clap_id id, double value) {
    switch (id) {
        case kVoiceParam: {
            const int voice = voiceIndex(plugin, value);
            if (voice >= 0) {
                return plugin.labels[static_cast<std::size_t>(voice)];
            }
            return std::string{plugin.storeLoaded ? "None" : kLoadingLabel};
        }
        case kFollowParam:
            return std::string{kSwitchLabels[value >= 0.5 ? 1 : 0]};
        case kVolumeParam:
            return std::format("{:.1f} dB", value);
        case kTuneParam:
            return std::format("{:+.2f} semitones", value);
        default:
            return value < 1.0 ? std::format("{:.0f} ms", value * 1000.0) : std::format("{:.2f} s", value);
    }
}

bool paramText(const clap_plugin* plugin, clap_id id, double value, char* text, std::uint32_t size) {
    if (id >= kParamCount || size == 0) {
        return false;
    }
    const std::string label = paramLabel(pluginOf(plugin), id, value);
    const std::size_t count = std::min<std::size_t>(label.size(), size - 1);
    std::ranges::copy(label.substr(0, count), text);
    std::span(text, size)[count] = '\0';
    return true;
}

bool paramFromText(const clap_plugin* plugin, clap_id id, const char* text, double* value) {
    const Plugin& voices = pluginOf(plugin);
    if (id == kVoiceParam || id == kFollowParam) {
        const std::size_t count = id == kVoiceParam ? voices.labels.size() + 1 : kSwitchLabels.size();
        const auto steps = std::views::iota(std::size_t{0}, count);
        const auto found = std::ranges::find_if(
            steps, [&](std::size_t step) { return paramLabel(voices, id, static_cast<double>(step)) == text; });
        if (found == steps.end()) {
            return false;
        }
        *value = static_cast<double>(*found);
        return true;
    }
    char* end = nullptr;
    const double number = std::strtod(text, &end);
    if (end == text || id >= kParamCount) {
        return false;
    }
    const bool milliseconds = (id == kAttackParam || id == kReleaseParam) && std::string_view(end).contains("ms");
    *value = milliseconds ? number / 1000.0 : number;
    return true;
}

void paramFlush(const clap_plugin* plugin, const clap_input_events* in, const clap_output_events* /*unused*/) {
    Plugin& voices = pluginOf(plugin);
    for (std::uint32_t index = 0; index < in->size(in); ++index) {
        const clap_event_header* header = in->get(in, index);
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_PARAM_VALUE) {
            setParam(voices, eventAs<clap_event_param_value>(header), voices.active);
        }
    }
}

}  // namespace

void setParam(Plugin& plugin, const clap_event_param_value& change, bool audioThread) {
    if (change.param_id == kVoiceParam) {
        selectVoice(plugin, voiceIndex(plugin, change.value), audioThread);
    } else if (change.param_id == kFollowParam) {
        plugin.followProgramChanges.store(change.value >= 0.5, std::memory_order_relaxed);
    } else if (const ParamRange* range = rangeOf(change.param_id)) {
        numberOf(plugin, change.param_id)
            ->store(static_cast<float>(std::clamp(change.value, range->min, range->max)), std::memory_order_relaxed);
    }
}

const clap_plugin_state kState{saveState, loadState};
const clap_plugin_params kParams{paramCount, paramInfo, paramValue, paramText, paramFromText, paramFlush};

}  // namespace emerick::voices
