// The General MIDI instrument's state and params. Its state is JSON with the SoundFont path, the sound and the drum
// kit, and a state that is no JSON object is a plain SoundFont path, as older projects saved it.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

#include "gm.hpp"
#include "gm_sounds.hpp"

namespace emerick::gm {

namespace {

bool saveState(const clap_plugin* plugin, const clap_ostream* stream) {
    const Gm& gm = gmOf(plugin);
    const std::string text = nlohmann::json{{"soundFont", gm.soundFont},
                                            {"sound", gm.sound.load(std::memory_order_relaxed)},
                                            {"drumKit", gm.drumKit.load(std::memory_order_relaxed)}}
                                 .dump();
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
    const auto number = [&](const char* key) {
        const auto found = state.find(key);
        return found != state.end() && found->is_number() ? found->get<double>() : 0.0;
    };
    const auto path = state.find("soundFont");
    Gm& gm = gmOf(plugin);
    gm.soundFont = path != state.end() && path->is_string() ? path->get<std::string>() : std::string{};
    gm.sound.store(clampedIndex(number("sound"), kSounds.size()), std::memory_order_relaxed);
    gm.drumKit.store(clampedIndex(number("drumKit"), kDrumKits.size()), std::memory_order_relaxed);
    gm.programsPending.store(true, std::memory_order_relaxed);
    if (gm.active && wantedSoundFont(gm) != gm.loadedSoundFont) {
        gm.host->request_restart(gm.host);
    }
    return true;
}

std::uint32_t paramCount(const clap_plugin* /*unused*/) { return 2; }

bool paramInfo(const clap_plugin* /*unused*/, std::uint32_t index, clap_param_info* info) {
    if (index > kDrumKitParam) {
        return false;
    }
    *info = {};
    info->id = index;
    info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM | CLAP_PARAM_IS_AUTOMATABLE;
    const bool sound = index == kSoundParam;
    std::strncpy(std::data(info->name), sound ? "Sound" : "Drum kit", std::size(info->name));
    info->max_value = static_cast<double>((sound ? kSounds.size() : kDrumKits.size()) - 1);
    return true;
}

bool paramValue(const clap_plugin* plugin, clap_id id, double* value) {
    const Gm& gm = gmOf(plugin);
    if (id > kDrumKitParam) {
        return false;
    }
    *value = id == kSoundParam ? gm.sound.load(std::memory_order_relaxed) : gm.drumKit.load(std::memory_order_relaxed);
    return true;
}

std::string_view paramLabel(clap_id id, double value) {
    return id == kSoundParam ? kSounds.at(static_cast<std::size_t>(clampedIndex(value, kSounds.size())))
                             : kDrumKits.at(static_cast<std::size_t>(clampedIndex(value, kDrumKits.size()))).name;
}

bool paramText(const clap_plugin* /*unused*/, clap_id id, double value, char* text, std::uint32_t size) {
    if (id > kDrumKitParam || size == 0) {
        return false;
    }
    const std::string_view label = paramLabel(id, value);
    const std::size_t count = std::min<std::size_t>(label.size(), size - 1);
    std::ranges::copy(label.substr(0, count), text);
    std::span(text, size)[count] = '\0';
    return true;
}

bool paramFromText(const clap_plugin* /*unused*/, clap_id id, const char* text, double* value) {
    if (id > kDrumKitParam) {
        return false;
    }
    const auto indices = std::views::iota(std::size_t{0}, id == kSoundParam ? kSounds.size() : kDrumKits.size());
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
    }
    gm.programsPending.store(true, std::memory_order_relaxed);
}

const clap_plugin_state kState{saveState, loadState};
const clap_plugin_params kParams{paramCount, paramInfo, paramValue, paramText, paramFromText, paramFlush};

}  // namespace emerick::gm
