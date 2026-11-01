// A tiny CLAP effect for the tests: it scales its stereo input by a Gain param its state saves.
// Built with EMERICK_TEST_LATENCY, it also delays its output by kLatency frames and reports that latency.

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace {

#ifdef EMERICK_TEST_LATENCY
constexpr std::uint32_t kLatency = 480;
constexpr const char* kId = "org.emerick.test.late";
constexpr const char* kName = "Test Late";
#else
constexpr std::uint32_t kLatency = 0;
constexpr const char* kId = "org.emerick.test.gain";
constexpr const char* kName = "Test Gain";
#endif

struct Gain {
    clap_plugin plugin{};
    double gain = 1.0;
    std::array<std::array<float, kLatency + 1>, 2> lines{};
    std::uint32_t write = 0;
};

Gain& gainOf(const clap_plugin* plugin) { return *static_cast<Gain*>(plugin->plugin_data); }

void handle(Gain& effect, const clap_event_header* header) {
    if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_PARAM_VALUE) {
        const auto* change = reinterpret_cast<const clap_event_param_value*>(header);
        if (change->param_id == 0) {
            effect.gain = change->value;
        }
    }
}

clap_process_status process(const clap_plugin* plugin, const clap_process* process) {
    Gain& effect = gainOf(plugin);
    for (std::uint32_t index = 0; index < process->in_events->size(process->in_events); ++index) {
        handle(effect, process->in_events->get(process->in_events, index));
    }
    float** inputs = process->audio_inputs[0].data32;
    float** outputs = process->audio_outputs[0].data32;
    for (std::uint32_t frame = 0; frame < process->frames_count; ++frame) {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            auto& line = effect.lines.at(channel);
            line.at(effect.write) = inputs[channel][frame] * static_cast<float>(effect.gain);
            outputs[channel][frame] = line.at((effect.write + 1) % line.size());
        }
        effect.write = (effect.write + 1) % static_cast<std::uint32_t>(effect.lines[0].size());
    }
    return CLAP_PROCESS_CONTINUE;
}

std::uint32_t audioPortCount(const clap_plugin*, bool) { return 1; }

bool audioPortGet(const clap_plugin*, std::uint32_t index, bool isInput, clap_audio_port_info* info) {
    if (index != 0) {
        return false;
    }
    *info = {};
    info->id = 0;
    std::strncpy(info->name, isInput ? "In" : "Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports kAudioPorts{audioPortCount, audioPortGet};

bool saveState(const clap_plugin* plugin, const clap_ostream* stream) {
    const double gain = gainOf(plugin).gain;
    return stream->write(stream, &gain, sizeof(gain)) == sizeof(gain);
}

bool loadState(const clap_plugin* plugin, const clap_istream* stream) {
    double gain = 0.0;
    if (stream->read(stream, &gain, sizeof(gain)) != sizeof(gain)) {
        return false;
    }
    gainOf(plugin).gain = gain;
    return true;
}

const clap_plugin_state kState{saveState, loadState};

std::uint32_t paramCount(const clap_plugin*) { return 1; }

bool paramInfo(const clap_plugin*, std::uint32_t index, clap_param_info* info) {
    if (index != 0) {
        return false;
    }
    *info = {};
    info->id = 0;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->name, "Gain", sizeof(info->name));
    info->min_value = 0.0;
    info->max_value = 2.0;
    info->default_value = 1.0;
    return true;
}

bool paramValue(const clap_plugin* plugin, clap_id id, double* value) {
    if (id != 0) {
        return false;
    }
    *value = gainOf(plugin).gain;
    return true;
}

bool paramText(const clap_plugin*, clap_id id, double value, char* text, std::uint32_t size) {
    return id == 0 && std::snprintf(text, size, "%.2fx", value) > 0;
}

void paramFlush(const clap_plugin* plugin, const clap_input_events* in, const clap_output_events*) {
    for (std::uint32_t index = 0; index < in->size(in); ++index) {
        handle(gainOf(plugin), in->get(in, index));
    }
}

bool paramFromText(const clap_plugin*, clap_id, const char*, double*) { return false; }

const clap_plugin_params kParams{paramCount, paramInfo, paramValue, paramText, paramFromText, paramFlush};

const clap_plugin_latency kLatencyExtension{[](const clap_plugin*) { return kLatency; }};

const void* extension(const clap_plugin*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &kAudioPorts;
    }
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
        return &kState;
    }
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &kParams;
    }
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) {
        return &kLatencyExtension;
    }
    return nullptr;
}

const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr};

const clap_plugin_descriptor kDescriptor{CLAP_VERSION_INIT,      kId,      kName, "Emerick", "", "", "", "1.0.0",
                                         "A gain for the tests", kFeatures};

const clap_plugin* createPlugin(const clap_plugin_factory*, const clap_host*, const char* id) {
    if (std::strcmp(id, kDescriptor.id) != 0) {
        return nullptr;
    }
    auto* effect = new Gain;
    effect->plugin = {&kDescriptor,
                      effect,
                      [](const clap_plugin*) { return true; },
                      [](const clap_plugin* plugin) { delete &gainOf(plugin); },
                      [](const clap_plugin*, double, std::uint32_t, std::uint32_t) { return true; },
                      [](const clap_plugin*) {},
                      [](const clap_plugin*) { return true; },
                      [](const clap_plugin*) {},
                      [](const clap_plugin*) {},
                      process,
                      extension,
                      [](const clap_plugin*) {}};
    return &effect->plugin;
}

const clap_plugin_factory kFactory{
    [](const clap_plugin_factory*) -> std::uint32_t { return 1; },
    [](const clap_plugin_factory*, std::uint32_t index) { return index == 0 ? &kDescriptor : nullptr; }, createPlugin};

}  // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry{
    CLAP_VERSION_INIT, [](const char*) { return true; }, [] {},
    [](const char* id) -> const void* { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr; }};
