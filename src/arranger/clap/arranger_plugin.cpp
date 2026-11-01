// The Arranger device: a CLAP plugin that plays a Yamaha style on the chord and section messages a Style track sends
// for the keyboard, following the host transport, and sends the style's parts on channels 9 to 16.
// The host's main thread reads the style file and builds its arranger; the audio thread plays the arranger it was
// handed and gives the last one back for the main thread to free.

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/arranger/arranger.hpp"
#include "emerick/arranger/style.hpp"
#include "emerick/core/base64.hpp"
#include "emerick/core/midi.hpp"
#include "emerick/yamaha/sysex.hpp"
#include "player.hpp"

namespace {

using emerick::arranger::Arranger;
using emerick::arranger::Player;
using emerick::arranger::Style;
using Json = nlohmann::json;

constexpr clap_id kAutoFillParam = 0;
constexpr std::uint8_t kButtonDown = 64;

struct Loaded {
    std::unique_ptr<Style> style;
    std::unique_ptr<Arranger> arranger;
};

struct Plugin {
    clap_plugin plugin{};
    const clap_host* host = nullptr;
    // Main thread only.
    std::string stylePath;
    std::string styleHash;
    std::vector<std::uint8_t> styleBytes;
    bool published = false;
    std::atomic<bool> autoFill{true};
    // The main thread hands a new arranger over through pending; the audio thread takes it when retired is empty and
    // leaves the one it played there.
    std::atomic<Loaded*> pending{nullptr};
    std::atomic<Loaded*> retired{nullptr};
    // Audio thread only.
    std::unique_ptr<Loaded> current;
    Player player;
    double sampleRate = 48000.0;
};

Plugin& pluginOf(const clap_plugin* plugin) { return *static_cast<Plugin*>(plugin->plugin_data); }

// CLAP hands every event as its header, and the type in that header says which struct it starts.
template <typename Event>
const Event& eventAs(const clap_event_header* header) {
    return *reinterpret_cast<const Event*>(header);
}

void freeRetired(Plugin& plugin) { std::unique_ptr<Loaded>{plugin.retired.exchange(nullptr)}.reset(); }

void publish(Plugin& plugin, std::unique_ptr<Loaded> next) {
    freeRetired(plugin);
    std::unique_ptr<Loaded>{plugin.pending.exchange(next.release())}.reset();
}

std::optional<std::vector<std::uint8_t>> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::optional<std::vector<std::uint8_t>> styleBytes(const Plugin& plugin) {
    if (!plugin.styleBytes.empty()) {
        return plugin.styleBytes;
    }
    return plugin.stylePath.empty() ? std::nullopt : readFile(plugin.stylePath);
}

// The style the state embeds plays first, else its file. A style that is missing or unreadable plays nothing, and the
// state keeps its path so the host can report it.
std::unique_ptr<Loaded> loadStyle(Plugin& plugin) {
    auto loaded = std::make_unique<Loaded>();
    const auto bytes = styleBytes(plugin);
    if (!bytes) {
        return loaded;
    }
    if (plugin.styleHash.empty()) {
        plugin.styleHash = emerick::arranger::styleHash(*bytes);
    }
    if (bytes->size() <= emerick::arranger::kEmbeddedStyleLimit) {
        plugin.styleBytes = *bytes;
    }
    try {
        loaded->style = std::make_unique<Style>(emerick::arranger::parseStyle(*bytes));
    } catch (const emerick::arranger::StyleError&) {
        return loaded;
    }
    loaded->arranger = std::make_unique<Arranger>(*loaded->style);
    return loaded;
}

void setParam(Plugin& plugin, const clap_event_header* header) {
    const auto& change = eventAs<clap_event_param_value>(header);
    if (change.param_id == kAutoFillParam) {
        plugin.autoFill.store(change.value >= 0.5, std::memory_order_relaxed);
    }
}

void receive(Plugin& plugin, const clap_event_header* header) {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) {
        return;
    }
    switch (header->type) {
        case CLAP_EVENT_PARAM_VALUE:
            setParam(plugin, header);
            return;
        case CLAP_EVENT_MIDI_SYSEX: {
            const auto& sysex = eventAs<clap_event_midi_sysex>(header);
            const std::span<const std::uint8_t> bytes(sysex.buffer, sysex.size);
            if (const auto chord = emerick::yamaha::decodeChordControl(bytes)) {
                plugin.player.chord(header->time, *chord);
            } else if (const auto press = emerick::yamaha::decodeSectionControl(bytes); press && press->on) {
                plugin.player.section(header->time, *press);
            }
            return;
        }
        case CLAP_EVENT_MIDI: {
            const auto& midi = eventAs<clap_event_midi>(header);
            if ((midi.data[0] & 0xF0) != emerick::core::midi::kControlChange || midi.data[2] < kButtonDown) {
                return;
            }
            if (const auto press = emerick::yamaha::decodeButtonPress(midi.data[1], plugin.player.playingMain())) {
                plugin.player.section(header->time, *press);
            }
            return;
        }
        default:
            return;
    }
}

Player::Block blockOf(const Plugin& plugin, const clap_process* process) {
    Player::Block block{.frames = process->frames_count};
    const clap_event_transport* transport = process->transport;
    if (transport == nullptr) {
        return block;
    }
    const auto ticks = [](clap_beattime beats) {
        return static_cast<double>(beats) * emerick::core::kTicksPerQuarter / CLAP_BEATTIME_FACTOR;
    };
    const std::uint32_t flags = transport->flags;
    const bool timed = (flags & CLAP_TRANSPORT_HAS_TEMPO) != 0 && (flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
    block.playing = timed && (flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
    block.ticks = ticks(transport->song_pos_beats);
    block.ticksPerFrame = transport->tempo / 60.0 * emerick::core::kTicksPerQuarter / plugin.sampleRate;
    if ((flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) != 0 && transport->tsig_denom > 0) {
        block.ticksPerBar = emerick::core::kTicksPerQuarter * 4 * transport->tsig_num / transport->tsig_denom;
    }
    block.looping = (flags & CLAP_TRANSPORT_IS_LOOP_ACTIVE) != 0;
    block.loopStart = ticks(transport->loop_start_beats);
    block.loopEnd = ticks(transport->loop_end_beats);
    return block;
}

void adoptPending(Plugin& plugin) {
    if (plugin.retired.load(std::memory_order_acquire) != nullptr) {
        return;
    }
    if (Loaded* next = plugin.pending.exchange(nullptr, std::memory_order_acq_rel)) {
        plugin.retired.store(plugin.current.release(), std::memory_order_release);
        plugin.current.reset(next);
        plugin.host->request_callback(plugin.host);
    }
}

clap_process_status process(const clap_plugin* clapPlugin, const clap_process* process) {
    Plugin& plugin = pluginOf(clapPlugin);
    adoptPending(plugin);
    Arranger* arranger = plugin.current ? plugin.current->arranger.get() : nullptr;
    plugin.player.begin(blockOf(plugin, process), arranger, plugin.autoFill.load(std::memory_order_relaxed));
    const std::uint32_t count = process->in_events->size(process->in_events);
    for (std::uint32_t index = 0; index < count; ++index) {
        receive(plugin, process->in_events->get(process->in_events, index));
    }
    for (const Player::Output& output : plugin.player.finish()) {
        clap_event_midi event{};
        event.header = {.size = sizeof(event),
                        .time = output.frame,
                        .space_id = CLAP_CORE_EVENT_SPACE_ID,
                        .type = CLAP_EVENT_MIDI,
                        .flags = 0};
        event.port_index = 0;
        std::memcpy(std::data(event.data), output.bytes.data(), output.bytes.size());
        process->out_events->try_push(process->out_events, &event.header);
    }
    return CLAP_PROCESS_CONTINUE;
}

std::uint32_t notePortCount(const clap_plugin* /*unused*/, bool /*unused*/) { return 1; }

bool notePortGet(const clap_plugin* /*unused*/, std::uint32_t index, bool isInput, clap_note_port_info* info) {
    if (index != 0) {
        return false;
    }
    *info = {};
    info->id = isInput ? 0 : 1;
    info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::strncpy(std::data(info->name), isInput ? "Style control" : "Parts", std::size(info->name));
    return true;
}

const clap_plugin_note_ports kNotePorts{notePortCount, notePortGet};

const clap_plugin_audio_ports kAudioPorts{
    [](const clap_plugin*, bool) -> std::uint32_t { return 0; },
    [](const clap_plugin*, std::uint32_t, bool, clap_audio_port_info*) { return false; }};

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

bool saveState(const clap_plugin* clapPlugin, const clap_ostream* stream) {
    const Plugin& plugin = pluginOf(clapPlugin);
    Json state{{"style", plugin.stylePath},
               {"hash", plugin.styleHash},
               {"autoFill", plugin.autoFill.load(std::memory_order_relaxed)}};
    if (!plugin.styleBytes.empty()) {
        state["data"] = emerick::core::toBase64(plugin.styleBytes);
    }
    return writeAll(stream, state.dump());
}

bool loadState(const clap_plugin* clapPlugin, const clap_istream* stream) {
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
    const Json state = Json::parse(text, nullptr, false);
    if (!state.is_object()) {
        return false;
    }
    Plugin& plugin = pluginOf(clapPlugin);
    const auto textOf = [&](const char* key) {
        const auto found = state.find(key);
        return found != state.end() && found->is_string() ? found->get<std::string>() : std::string{};
    };
    const auto autoFill = state.find("autoFill");
    plugin.autoFill.store(autoFill == state.end() || !autoFill->is_boolean() || autoFill->get<bool>(),
                          std::memory_order_relaxed);
    const std::string path = textOf("style");
    const std::string hash = textOf("hash");
    std::vector<std::uint8_t> data = emerick::core::fromBase64(textOf("data")).value_or(std::vector<std::uint8_t>{});
    // A state that only changed a param keeps the arranger playing.
    if (plugin.published && path == plugin.stylePath && (hash.empty() || hash == plugin.styleHash)) {
        return true;
    }
    plugin.stylePath = path;
    plugin.styleHash = data.empty() || !hash.empty() ? hash : emerick::arranger::styleHash(data);
    plugin.styleBytes = std::move(data);
    publish(plugin, loadStyle(plugin));
    plugin.published = true;
    return true;
}

const clap_plugin_state kState{saveState, loadState};

constexpr std::array<std::string_view, 2> kSwitchLabels{"Off", "On"};

bool paramInfo(const clap_plugin* /*unused*/, std::uint32_t index, clap_param_info* info) {
    if (index != 0) {
        return false;
    }
    *info = {};
    info->id = kAutoFillParam;
    info->flags = CLAP_PARAM_IS_STEPPED;
    std::strncpy(std::data(info->name), "Auto fill", std::size(info->name));
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = 1.0;
    return true;
}

bool paramValue(const clap_plugin* clapPlugin, clap_id id, double* value) {
    if (id != kAutoFillParam) {
        return false;
    }
    *value = pluginOf(clapPlugin).autoFill.load(std::memory_order_relaxed) ? 1.0 : 0.0;
    return true;
}

bool paramText(const clap_plugin* /*unused*/, clap_id id, double value, char* text, std::uint32_t size) {
    if (id != kAutoFillParam || size == 0) {
        return false;
    }
    const std::string_view label = kSwitchLabels.at(value >= 0.5 ? 1 : 0);
    const std::span<char> out(text, size);
    const std::size_t length = std::min<std::size_t>(label.size(), size - 1);
    std::ranges::copy(label.substr(0, length), out.begin());
    out[length] = '\0';
    return true;
}

bool paramFromText(const clap_plugin* /*unused*/, clap_id id, const char* text, double* value) {
    if (id != kAutoFillParam) {
        return false;
    }
    const auto* const found = std::ranges::find(kSwitchLabels, std::string_view{text});
    if (found == kSwitchLabels.end()) {
        return false;
    }
    *value = static_cast<double>(std::distance(kSwitchLabels.begin(), found));
    return true;
}

void paramFlush(const clap_plugin* clapPlugin, const clap_input_events* in, const clap_output_events* /*unused*/) {
    const std::uint32_t count = in->size(in);
    for (std::uint32_t index = 0; index < count; ++index) {
        const clap_event_header* header = in->get(in, index);
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_PARAM_VALUE) {
            setParam(pluginOf(clapPlugin), header);
        }
    }
}

const clap_plugin_params kParams{
    [](const clap_plugin*) -> std::uint32_t { return 1; }, paramInfo, paramValue, paramText, paramFromText, paramFlush};

const void* extension(const clap_plugin* /*unused*/, const char* id) {
    if (std::strcmp(id, std::data(CLAP_EXT_AUDIO_PORTS)) == 0) {
        return &kAudioPorts;
    }
    if (std::strcmp(id, std::data(CLAP_EXT_NOTE_PORTS)) == 0) {
        return &kNotePorts;
    }
    if (std::strcmp(id, std::data(CLAP_EXT_STATE)) == 0) {
        return &kState;
    }
    if (std::strcmp(id, std::data(CLAP_EXT_PARAMS)) == 0) {
        return &kParams;
    }
    return nullptr;
}

constexpr std::array<const char*, 2> kFeatures{CLAP_PLUGIN_FEATURE_NOTE_EFFECT, nullptr};

const clap_plugin_descriptor kDescriptor{CLAP_VERSION_INIT,
                                         "org.emerick.arranger",
                                         "Arranger",
                                         "Emerick",
                                         "",
                                         "",
                                         "",
                                         "1.0.0",
                                         "Plays a Yamaha style on the chords and sections of the Style track",
                                         kFeatures.data()};

void destroy(const clap_plugin* clapPlugin) {
    std::unique_ptr<Plugin> plugin{&pluginOf(clapPlugin)};
    freeRetired(*plugin);
    std::unique_ptr<Loaded>{plugin->pending.exchange(nullptr)}.reset();
}

bool activate(const clap_plugin* clapPlugin, double sampleRate, std::uint32_t /*unused*/, std::uint32_t /*unused*/) {
    pluginOf(clapPlugin).sampleRate = sampleRate;
    return true;
}

const clap_plugin* createPlugin(const clap_plugin_factory* /*unused*/, const clap_host* host, const char* id) {
    if (std::strcmp(id, kDescriptor.id) != 0) {
        return nullptr;
    }
    auto plugin = std::make_unique<Plugin>();
    plugin->host = host;
    plugin->plugin = {&kDescriptor,
                      plugin.get(),
                      [](const clap_plugin*) { return true; },
                      destroy,
                      activate,
                      [](const clap_plugin*) {},
                      [](const clap_plugin*) { return true; },
                      [](const clap_plugin*) {},
                      [](const clap_plugin*) {},
                      process,
                      extension,
                      [](const clap_plugin* clapPlugin) { freeRetired(pluginOf(clapPlugin)); }};
    return &plugin.release()->plugin;
}

const clap_plugin_factory kFactory{
    [](const clap_plugin_factory*) -> std::uint32_t { return 1; },
    [](const clap_plugin_factory*, std::uint32_t index) { return index == 0 ? &kDescriptor : nullptr; }, createPlugin};

const void* factory(const char* id) {
    return std::strcmp(id, std::data(CLAP_PLUGIN_FACTORY_ID)) == 0 ? &kFactory : nullptr;
}

}  // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry{CLAP_VERSION_INIT, [](const char*) { return true; }, [] {},
                                                          factory};
