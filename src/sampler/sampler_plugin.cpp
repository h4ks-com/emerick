// The Emerick Sampler: a CLAP plugin that plays one SFZ instrument through sfizz.
// A thread of its own loads the instrument the state names, the host's main thread swaps it in while the plugin is
// inactive, and the audio thread only plays the instrument it was handed.

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <sfizz.hpp>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include "sampler_params.hpp"

namespace {

using emerick::sampler::kLoadingParam;
using emerick::sampler::kParamCount;
using emerick::sampler::kProblemParam;

constexpr std::uint32_t kDefaultBlock = 1024;
// sfizz renders exports at sample quality 10 by default, which takes twice as long as 3 on a busy kit for no audible
// gain.
constexpr int kRenderSampleQuality = 3;
constexpr int kAllNotesOff = 123;
constexpr int kBendCenter = 8192;
constexpr std::uint8_t kStatusMask = 0xF0;
constexpr std::uint8_t kNoteOff = 0x80;
constexpr std::uint8_t kNoteOn = 0x90;
constexpr std::uint8_t kPolyPressure = 0xA0;
constexpr std::uint8_t kControlChange = 0xB0;
constexpr std::uint8_t kProgramChange = 0xC0;
constexpr std::uint8_t kChannelPressure = 0xD0;
constexpr std::uint8_t kPitchBend = 0xE0;
constexpr int kDataBits = 7;

struct Instrument {
    // Null plays silence.
    std::unique_ptr<sfz::Sfizz> synth;
    // Why the instrument plays nothing, or empty.
    std::string problem;
};

struct Load {
    std::string path;
    std::mutex mutex;
    std::optional<Instrument> loaded;
    // We declare the thread last so it joins before the members it uses are destroyed.
    std::jthread thread;
};

struct Plugin {
    clap_plugin plugin{};
    const clap_host* host = nullptr;
    // The SFZ file the state names, as UTF-8; empty plays nothing.
    std::string path;
    std::unique_ptr<Load> load;
    std::optional<std::string> playingPath;
    // The audio thread plays it, and the main thread replaces it only while the plugin is inactive.
    Instrument instrument;
    double sampleRate = 48000.0;
    std::uint32_t maxFrames = kDefaultBlock;
    bool active = false;
    // sfizz streams samples from disk while it plays live, and in an export it waits for each one to load.
    std::atomic<bool> offline{false};
    bool freewheeling = false;
};

Plugin& pluginOf(const clap_plugin* plugin) { return *static_cast<Plugin*>(plugin->plugin_data); }

// CLAP hands every event as its header, and the type in that header says which struct it starts.
template <typename Event>
const Event& eventAs(const clap_event_header* header) {
    return *reinterpret_cast<const Event*>(header);
}

Instrument loadInstrument(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(std::filesystem::path{std::u8string{path.begin(), path.end()}}, error)) {
        return {.synth = nullptr, .problem = "The SFZ file " + path + " is gone. Run sampler.load with its new path."};
    }
    auto synth = std::make_unique<sfz::Sfizz>();
    synth->setSampleQuality(sfz::Sfizz::ProcessFreewheeling, kRenderSampleQuality);
    if (!synth->loadSfzFile(path) || synth->getNumRegions() == 0) {
        return {.synth = nullptr, .problem = "The SFZ file " + path + " plays no samples. Load another SFZ file."};
    }
    return {.synth = std::move(synth), .problem = {}};
}

void configure(Plugin& plugin) {
    if (plugin.instrument.synth) {
        plugin.instrument.synth->setSampleRate(static_cast<float>(plugin.sampleRate));
        plugin.instrument.synth->setSamplesPerBlock(static_cast<int>(plugin.maxFrames));
    }
}

// Main thread: loads the instrument the state names on a thread of its own, unless it plays or loads already.
void startLoad(Plugin& plugin) {
    if (plugin.load ? plugin.load->path == plugin.path : plugin.playingPath == plugin.path) {
        return;
    }
    plugin.load = std::make_unique<Load>();
    plugin.load->path = plugin.path;
    // NOLINTNEXTLINE(bugprone-exception-escape): running out of memory while loading samples ends the process.
    plugin.load->thread = std::jthread([&load = *plugin.load, host = plugin.host] {
        Instrument loaded = loadInstrument(load.path);
        {
            const std::scoped_lock lock(load.mutex);
            load.loaded = std::move(loaded);
        }
        host->request_callback(host);
    });
}

// Main thread: swaps in a loaded instrument while the plugin is inactive, and asks an active one for a restart.
void publishLoaded(Plugin& plugin) {
    if (!plugin.load) {
        return;
    }
    std::optional<Instrument> loaded;
    {
        const std::scoped_lock lock(plugin.load->mutex);
        if (!plugin.load->loaded) {
            return;
        }
        if (plugin.active) {
            plugin.host->request_restart(plugin.host);
            return;
        }
        loaded = std::move(plugin.load->loaded);
    }
    plugin.playingPath = plugin.load->path;
    plugin.load.reset();
    plugin.instrument = std::move(*loaded);
    plugin.freewheeling = false;
    configure(plugin);
    if (const auto* params =
            static_cast<const clap_host_params*>(plugin.host->get_extension(plugin.host, std::data(CLAP_EXT_PARAMS)))) {
        params->rescan(plugin.host, CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);
    }
}

bool activate(const clap_plugin* clapPlugin, double sampleRate, std::uint32_t /*unused*/, std::uint32_t maxFrames) {
    Plugin& plugin = pluginOf(clapPlugin);
    plugin.sampleRate = sampleRate;
    plugin.maxFrames = maxFrames;
    publishLoaded(plugin);
    configure(plugin);
    plugin.active = true;
    return true;
}

void playMidi(sfz::Sfizz& synth, int delay, const std::array<std::uint8_t, 3>& data) {
    const int first = data[1];
    const int second = data[2];
    switch (data[0] & kStatusMask) {
        case kNoteOff:
            synth.noteOff(delay, first, second);
            break;
        case kNoteOn:
            if (second == 0) {
                synth.noteOff(delay, first, 0);
            } else {
                synth.noteOn(delay, first, second);
            }
            break;
        case kPolyPressure:
            synth.polyAftertouch(delay, first, second);
            break;
        case kControlChange:
            synth.cc(delay, first, second);
            break;
        case kProgramChange:
            synth.programChange(delay, first);
            break;
        case kChannelPressure:
            synth.channelAftertouch(delay, first);
            break;
        case kPitchBend:
            synth.pitchWheel(delay, ((second << kDataBits) | first) - kBendCenter);
            break;
        default:
            break;
    }
}

void playNote(sfz::Sfizz& synth, int delay, const clap_event_note& note, bool on) {
    if (note.key < 0) {
        if (!on) {
            synth.cc(delay, kAllNotesOff, 0);
        }
        return;
    }
    if (on) {
        synth.hdNoteOn(delay, note.key, static_cast<float>(note.velocity));
    } else {
        synth.hdNoteOff(delay, note.key, static_cast<float>(note.velocity));
    }
}

void play(sfz::Sfizz& synth, const clap_event_header* header, std::uint32_t frames) {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) {
        return;
    }
    const int delay = static_cast<int>(std::min(header->time, frames - 1));
    switch (header->type) {
        case CLAP_EVENT_NOTE_ON:
            playNote(synth, delay, eventAs<clap_event_note>(header), true);
            break;
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE:
            playNote(synth, delay, eventAs<clap_event_note>(header), false);
            break;
        case CLAP_EVENT_MIDI:
            playMidi(synth, delay, std::to_array(eventAs<clap_event_midi>(header).data));
            break;
        default:
            break;
    }
}

void followRenderMode(Plugin& plugin, sfz::Sfizz& synth) {
    const bool offline = plugin.offline.load(std::memory_order_relaxed);
    if (offline == plugin.freewheeling) {
        return;
    }
    if (offline) {
        synth.enableFreeWheeling();
    } else {
        synth.disableFreeWheeling();
    }
    plugin.freewheeling = offline;
}

clap_process_status process(const clap_plugin* clapPlugin, const clap_process* process) {
    Plugin& plugin = pluginOf(clapPlugin);
    sfz::Sfizz* synth = plugin.instrument.synth.get();
    const std::span<float*> outputs(process->audio_outputs[0].data32, 2);
    const std::uint32_t frames = process->frames_count;
    if (synth == nullptr || frames == 0) {
        for (float* channel : outputs) {
            std::fill_n(channel, frames, 0.0F);
        }
        return CLAP_PROCESS_CONTINUE;
    }
    followRenderMode(plugin, *synth);
    const std::uint32_t events = process->in_events->size(process->in_events);
    for (std::uint32_t index = 0; index < events; ++index) {
        play(*synth, process->in_events->get(process->in_events, index), frames);
    }
    synth->renderBlock(outputs.data(), frames, 1);
    return CLAP_PROCESS_CONTINUE;
}

bool saveState(const clap_plugin* clapPlugin, const clap_ostream* stream) {
    const std::string text = nlohmann::json{{"path", pluginOf(clapPlugin).path}}.dump();
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
    const nlohmann::json state = nlohmann::json::parse(text, nullptr, false);
    const auto path = state.is_object() ? state.find("path") : state.end();
    Plugin& plugin = pluginOf(clapPlugin);
    plugin.path = path != state.end() && path->is_string() ? path->get<std::string>() : std::string{};
    startLoad(plugin);
    return true;
}

const clap_plugin_state kState{saveState, loadState};

constexpr std::array<const char*, kParamCount> kParamNames{"Problem", "Loading"};

std::uint32_t paramCount(const clap_plugin* /*unused*/) { return kParamCount; }

bool paramInfo(const clap_plugin* /*unused*/, std::uint32_t index, clap_param_info* info) {
    if (index >= kParamCount) {
        return false;
    }
    *info = {};
    info->id = index;
    info->flags = CLAP_PARAM_IS_HIDDEN | CLAP_PARAM_IS_READONLY;
    std::strncpy(std::data(info->name), kParamNames.at(index), std::size(info->name) - 1);
    info->max_value = 1.0;
    return true;
}

bool paramValue(const clap_plugin* clapPlugin, clap_id id, double* value) {
    if (id >= kParamCount) {
        return false;
    }
    *value = id == kLoadingParam && pluginOf(clapPlugin).load ? 1.0 : 0.0;
    return true;
}

std::string paramLabel(const Plugin& plugin, clap_id id, double value) {
    if (id == kProblemParam) {
        return plugin.instrument.problem;
    }
    return value >= 0.5 ? "Loading" : "";
}

bool paramText(const clap_plugin* clapPlugin, clap_id id, double value, char* text, std::uint32_t size) {
    if (id >= kParamCount || size == 0) {
        return false;
    }
    const std::string label = paramLabel(pluginOf(clapPlugin), id, value);
    const std::size_t count = std::min<std::size_t>(label.size(), size - 1);
    std::ranges::copy(label.substr(0, count), text);
    std::span(text, size)[count] = '\0';
    return true;
}

const clap_plugin_params kParams{paramCount,
                                 paramInfo,
                                 paramValue,
                                 paramText,
                                 [](const clap_plugin*, clap_id, const char*, double*) { return false; },
                                 [](const clap_plugin*, const clap_input_events*, const clap_output_events*) {}};

std::uint32_t audioPortCount(const clap_plugin* /*unused*/, bool isInput) { return isInput ? 0 : 1; }

bool audioPortGet(const clap_plugin* /*unused*/, std::uint32_t index, bool isInput, clap_audio_port_info* info) {
    if (isInput || index != 0) {
        return false;
    }
    *info = {};
    std::strncpy(std::data(info->name), "Out", std::size(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = std::data(CLAP_PORT_STEREO);
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports kAudioPorts{audioPortCount, audioPortGet};

std::uint32_t notePortCount(const clap_plugin* /*unused*/, bool isInput) { return isInput ? 1 : 0; }

bool notePortGet(const clap_plugin* /*unused*/, std::uint32_t index, bool isInput, clap_note_port_info* info) {
    if (!isInput || index != 0) {
        return false;
    }
    *info = {};
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::strncpy(std::data(info->name), "MIDI", std::size(info->name));
    return true;
}

const clap_plugin_note_ports kNotePorts{notePortCount, notePortGet};

bool hasHardRealtimeRequirement(const clap_plugin* /*unused*/) { return false; }

bool setRenderMode(const clap_plugin* clapPlugin, clap_plugin_render_mode mode) {
    pluginOf(clapPlugin).offline.store(mode == CLAP_RENDER_OFFLINE, std::memory_order_relaxed);
    return true;
}

const clap_plugin_render kRender{hasHardRealtimeRequirement, setRenderMode};

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
    if (std::strcmp(id, std::data(CLAP_EXT_RENDER)) == 0) {
        return &kRender;
    }
    return nullptr;
}

constexpr std::array<const char*, 4> kFeatures{CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SAMPLER,
                                               CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor kDescriptor{CLAP_VERSION_INIT,
                                         "org.emerick.sampler",
                                         "Emerick Sampler",
                                         "Emerick",
                                         "",
                                         "",
                                         "",
                                         "1.0.0",
                                         "Plays an SFZ instrument through sfizz",
                                         kFeatures.data()};

const clap_plugin* createPlugin(const clap_plugin_factory* /*unused*/, const clap_host* host, const char* id) {
    if (std::strcmp(id, kDescriptor.id) != 0) {
        return nullptr;
    }
    auto sampler = std::make_unique<Plugin>();
    sampler->host = host;
    sampler->plugin = {&kDescriptor,
                       sampler.get(),
                       [](const clap_plugin*) { return true; },
                       [](const clap_plugin* plugin) { std::unique_ptr<Plugin>{&pluginOf(plugin)}.reset(); },
                       activate,
                       [](const clap_plugin* plugin) { pluginOf(plugin).active = false; },
                       [](const clap_plugin*) { return true; },
                       [](const clap_plugin*) {},
                       [](const clap_plugin* plugin) {
                           if (sfz::Sfizz* synth = pluginOf(plugin).instrument.synth.get()) {
                               synth->allSoundOff();
                           }
                       },
                       process,
                       extension,
                       [](const clap_plugin* plugin) { publishLoaded(pluginOf(plugin)); }};
    return &sampler.release()->plugin;
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
