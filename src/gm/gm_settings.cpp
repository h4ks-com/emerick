// The General MIDI instrument's state and params. Its state is JSON with the SoundFont path, the sound, the drum kit,
// Follow program changes, the highest note, Play drums on every channel and an optional drum note map, and a state that
// is no JSON object is a plain SoundFont path, as older projects saved it.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "gm.hpp"
#include "gm_sounds.hpp"

namespace emerick::gm {

namespace {

nlohmann::json drumMap(const Gm& gm) {
    nlohmann::json map = nlohmann::json::array();
    bool changed = false;
    for (const int note : std::views::iota(0, kNotes)) {
        const int offset = gm.drumNoteOffsets.at(static_cast<std::size_t>(note)).load(std::memory_order_relaxed);
        changed = changed || offset != 0;
        map.push_back(std::max(note + offset, -1));
    }
    return changed ? map : nlohmann::json{};
}

// A drum map that is no array of 128 notes from -1 to 127 plays every note unchanged.
void loadDrumMap(Gm& gm, const nlohmann::json& state) {
    const auto map = state.find(kDrumMapKey);
    const bool valid = map != state.end() && map->is_array() && map->size() == kNotes &&
                       std::ranges::all_of(*map, [](const nlohmann::json& note) {
                           return note.is_number_integer() && note.get<int>() >= -1 && note.get<int>() < kNotes;
                       });
    for (const int note : std::views::iota(0, kNotes)) {
        const int played = valid ? map->at(static_cast<std::size_t>(note)).get<int>() : note;
        gm.drumNoteOffsets.at(static_cast<std::size_t>(note)).store(played - note, std::memory_order_relaxed);
    }
}

bool saveState(const clap_plugin* plugin, const clap_ostream* stream) {
    const Gm& gm = gmOf(plugin);
    nlohmann::json state{{"soundFont", gm.soundFont},
                         {"sound", gm.sound.load(std::memory_order_relaxed)},
                         {"drumKit", gm.drumKit.load(std::memory_order_relaxed)},
                         {"followProgramChanges", gm.followProgramChanges.load(std::memory_order_relaxed)},
                         {"highestNote", gm.highestNote.load(std::memory_order_relaxed)},
                         {"drumsEverywhere", gm.drumsEverywhere.load(std::memory_order_relaxed)}};
    if (nlohmann::json map = drumMap(gm); !map.is_null()) {
        state[kDrumMapKey] = std::move(map);
    }
    const std::string text = state.dump();
    std::size_t written = 0;
    while (written < text.size()) {
        const std::int64_t count = stream->write(stream, text.data() + written, text.size() - written);
        if (count <= 0) {
            return false;
        }
        written += static_cast<std::size_t>(count);
    }
    return true;
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
    nlohmann::json state = nlohmann::json::parse(text, nullptr, false);
    if (!state.is_object()) {
        state = {{"soundFont", text}};
    }
    const auto number = [&](const char* key, double fallback = 0.0) {
        const auto found = state.find(key);
        return found != state.end() && found->is_number() ? found->get<double>() : fallback;
    };
    const auto path = state.find("soundFont");
    Gm& gm = gmOf(plugin);
    gm.soundFont = path != state.end() && path->is_string() ? path->get<std::string>() : std::string{};
    gm.sound.store(clampedIndex(number("sound"), kSounds.size()), std::memory_order_relaxed);
    gm.drumKit.store(clampedIndex(number("drumKit"), kDrumKits.size()), std::memory_order_relaxed);
    const auto follow = state.find("followProgramChanges");
    gm.followProgramChanges.store(follow == state.end() || !follow->is_boolean() || follow->get<bool>(),
                                  std::memory_order_relaxed);
    const auto drumsEverywhere = state.find("drumsEverywhere");
    gm.drumsEverywhere.store(
        drumsEverywhere != state.end() && drumsEverywhere->is_boolean() && drumsEverywhere->get<bool>(),
        std::memory_order_relaxed);
    gm.highestNote.store(clampedIndex(number("highestNote", kHighestNote), kHighestNote + 1),
                         std::memory_order_relaxed);
    loadDrumMap(gm, state);
    gm.programsPending.store(true, std::memory_order_relaxed);
    if (gm.active && wantedSoundFont(gm) != gm.loadedSoundFont) {
        gm.host->request_restart(gm.host);
    }
    return true;
}

constexpr std::array<std::string_view, 2> kSwitchLabels{"Off", "On"};
constexpr std::array<const char*, 5> kParamNames{"Sound", "Drum kit", "Follow program changes", "Highest note",
                                                 "Play drums on every channel"};
static_assert(kDrumChannel == 9, "the drum module names the drum channel");
constexpr const char* kDrumModule = "Channel 10";

std::size_t choiceCount(clap_id id) {
    switch (id) {
        case kSoundParam:
            return kSounds.size();
        case kDrumKitParam:
            return kDrumKits.size();
        case kHighestNoteParam:
            return kHighestNote + 1;
        default:
            return kSwitchLabels.size();
    }
}

std::uint32_t paramCount(const clap_plugin* /*unused*/) { return kParamNames.size(); }

bool paramInfo(const clap_plugin* /*unused*/, std::uint32_t index, clap_param_info* info) {
    if (index >= kParamNames.size()) {
        return false;
    }
    *info = {};
    info->id = index;
    info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
    if (index != kHighestNoteParam) {
        info->flags |= CLAP_PARAM_IS_ENUM;
    }
    std::strncpy(std::data(info->name), kParamNames.at(index), std::size(info->name) - 1);
    // We name the drum channel as the module of the params that set it, so a host auditions a drum kit on channel 10
    // and turns Play drums on every channel off to audition a Sound.
    if (index == kDrumKitParam || index == kDrumsEverywhereParam) {
        std::strncpy(std::data(info->module), kDrumModule, std::size(info->module));
    }
    info->max_value = static_cast<double>(choiceCount(index) - 1);
    if (index == kFollowParam) {
        info->default_value = 1.0;
    } else if (index == kHighestNoteParam) {
        info->default_value = kHighestNote;
    }
    return true;
}

bool paramValue(const clap_plugin* plugin, clap_id id, double* value) {
    const Gm& gm = gmOf(plugin);
    switch (id) {
        case kSoundParam:
            *value = gm.sound.load(std::memory_order_relaxed);
            return true;
        case kDrumKitParam:
            *value = gm.drumKit.load(std::memory_order_relaxed);
            return true;
        case kFollowParam:
            *value = gm.followProgramChanges.load(std::memory_order_relaxed) ? 1.0 : 0.0;
            return true;
        case kHighestNoteParam:
            *value = gm.highestNote.load(std::memory_order_relaxed);
            return true;
        case kDrumsEverywhereParam:
            *value = gm.drumsEverywhere.load(std::memory_order_relaxed) ? 1.0 : 0.0;
            return true;
        default:
            return false;
    }
}

std::string paramLabel(clap_id id, double value) {
    const auto index = static_cast<std::size_t>(clampedIndex(value, choiceCount(id)));
    switch (id) {
        case kSoundParam:
            return std::string{kSounds.at(index)};
        case kDrumKitParam:
            return std::string{kDrumKits.at(index).name};
        case kHighestNoteParam:
            return std::to_string(index);
        default:
            return std::string{kSwitchLabels.at(index)};
    }
}

bool paramText(const clap_plugin* /*unused*/, clap_id id, double value, char* text, std::uint32_t size) {
    if (id >= kParamNames.size() || size == 0) {
        return false;
    }
    const std::string label = paramLabel(id, value);
    const std::size_t count = std::min<std::size_t>(label.size(), size - 1);
    std::ranges::copy(label.substr(0, count), text);
    std::span(text, size)[count] = '\0';
    return true;
}

bool paramFromText(const clap_plugin* /*unused*/, clap_id id, const char* text, double* value) {
    if (id >= kParamNames.size()) {
        return false;
    }
    const auto indices = std::views::iota(std::size_t{0}, choiceCount(id));
    const auto found = std::ranges::find_if(
        indices, [&](std::size_t index) { return paramLabel(id, static_cast<double>(index)) == text; });
    if (found == indices.end()) {
        return false;
    }
    *value = static_cast<double>(*found);
    return true;
}

void paramFlush(const clap_plugin* plugin, const clap_input_events* in, const clap_output_events* /*unused*/) {
    for (std::uint32_t index = 0; index < in->size(in); ++index) {
        const clap_event_header* header = in->get(in, index);
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_PARAM_VALUE) {
            setParam(gmOf(plugin), eventAs<clap_event_param_value>(header));
        }
    }
}

}  // namespace

void setParam(Gm& gm, const clap_event_param_value& change) {
    if (change.param_id == kSoundParam) {
        gm.sound.store(clampedIndex(change.value, kSounds.size()), std::memory_order_relaxed);
    } else if (change.param_id == kDrumKitParam) {
        gm.drumKit.store(clampedIndex(change.value, kDrumKits.size()), std::memory_order_relaxed);
    } else if (change.param_id == kFollowParam) {
        gm.followProgramChanges.store(change.value >= 0.5, std::memory_order_relaxed);
    } else if (change.param_id == kDrumsEverywhereParam) {
        gm.drumsEverywhere.store(change.value >= 0.5, std::memory_order_relaxed);
    } else if (change.param_id == kHighestNoteParam) {
        gm.highestNote.store(clampedIndex(change.value, kHighestNote + 1), std::memory_order_relaxed);
        return;
    }
    gm.programsPending.store(true, std::memory_order_relaxed);
}

const clap_plugin_state kState{saveState, loadState};
const clap_plugin_params kParams{paramCount, paramInfo, paramValue, paramText, paramFromText, paramFlush};

}  // namespace emerick::gm
