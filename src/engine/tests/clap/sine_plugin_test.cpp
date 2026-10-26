// A tiny CLAP instrument for the tests: one sine voice at the last note held, scaled by a level its state saves.
// Built with EMERICK_TEST_CRASH or EMERICK_TEST_HANG, its entry crashes or hangs, as a broken plugin would.

#include <clap/clap.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <thread>

namespace {

struct Synth {
    clap_plugin plugin{};
    double sampleRate = 48000.0;
    double phase = 0.0;
    double frequency = 0.0;
    int key = -1;
    float level = 0.5F;
};

Synth& synthOf(const clap_plugin* plugin) { return *static_cast<Synth*>(plugin->plugin_data); }

void noteOn(Synth& synth, int key) {
    synth.key = key;
    synth.frequency = 440.0 * std::pow(2.0, (key - 69) / 12.0);
}

void noteOff(Synth& synth, int key) {
    if (key == synth.key || key < 0) {
        synth.key = -1;
    }
}

void handle(Synth& synth, const clap_event_header* header) {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) {
        return;
    }
    if (header->type == CLAP_EVENT_NOTE_ON) {
        noteOn(synth, reinterpret_cast<const clap_event_note*>(header)->key);
    } else if (header->type == CLAP_EVENT_NOTE_OFF) {
        noteOff(synth, reinterpret_cast<const clap_event_note*>(header)->key);
    } else if (header->type == CLAP_EVENT_MIDI) {
        const auto* midi = reinterpret_cast<const clap_event_midi*>(header);
        const int type = midi->data[0] & 0xF0;
        if (type == 0x90 && midi->data[2] > 0) {
            noteOn(synth, midi->data[1]);
        } else if (type == 0x80 || type == 0x90) {
            noteOff(synth, midi->data[1]);
        } else if (type == 0xB0 && midi->data[1] == 123) {
            noteOff(synth, -1);
        }
    }
}

clap_process_status process(const clap_plugin* plugin, const clap_process* process) {
    Synth& synth = synthOf(plugin);
    const std::uint32_t events = process->in_events->size(process->in_events);
    std::uint32_t next = 0;
    float** outputs = process->audio_outputs[0].data32;
    for (std::uint32_t frame = 0; frame < process->frames_count; ++frame) {
        while (next < events) {
            const clap_event_header* header = process->in_events->get(process->in_events, next);
            if (header->time > frame) {
                break;
            }
            handle(synth, header);
            ++next;
        }
        float sample = 0.0F;
        if (synth.key >= 0) {
            sample = synth.level * static_cast<float>(std::sin(synth.phase));
            synth.phase += 2.0 * std::numbers::pi * synth.frequency / synth.sampleRate;
        }
        outputs[0][frame] = sample;
        outputs[1][frame] = sample;
    }
    return CLAP_PROCESS_CONTINUE;
}

std::uint32_t audioPortCount(const clap_plugin*, bool isInput) { return isInput ? 0 : 1; }

bool audioPortGet(const clap_plugin*, std::uint32_t index, bool isInput, clap_audio_port_info* info) {
    if (isInput || index != 0) {
        return false;
    }
    *info = {};
    info->id = 0;
    std::strncpy(info->name, "Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports kAudioPorts{audioPortCount, audioPortGet};

std::uint32_t notePortCount(const clap_plugin*, bool isInput) { return isInput ? 1 : 0; }

bool notePortGet(const clap_plugin*, std::uint32_t index, bool isInput, clap_note_port_info* info) {
    if (!isInput || index != 0) {
        return false;
    }
    *info = {};
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "Notes", sizeof(info->name));
    return true;
}

const clap_plugin_note_ports kNotePorts{notePortCount, notePortGet};

bool saveState(const clap_plugin* plugin, const clap_ostream* stream) {
    const float level = synthOf(plugin).level;
    return stream->write(stream, &level, sizeof(level)) == sizeof(level);
}

bool loadState(const clap_plugin* plugin, const clap_istream* stream) {
    float level = 0.0F;
    if (stream->read(stream, &level, sizeof(level)) != sizeof(level)) {
        return false;
    }
    synthOf(plugin).level = level;
    return true;
}

const clap_plugin_state kState{saveState, loadState};

const void* extension(const clap_plugin*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &kAudioPorts;
    }
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) {
        return &kNotePorts;
    }
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
        return &kState;
    }
    return nullptr;
}

const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr};

const clap_plugin_descriptor kDescriptor{
    CLAP_VERSION_INIT, "org.emerick.test.sine",      "Test Sine", "Emerick", "", "", "",
    "1.0.0",           "A sine voice for the tests", kFeatures};

const clap_plugin* createPlugin(const clap_plugin_factory*, const clap_host*, const char* id) {
    if (std::strcmp(id, kDescriptor.id) != 0) {
        return nullptr;
    }
    auto* synth = new Synth;
    synth->plugin = {&kDescriptor,
                     synth,
                     [](const clap_plugin*) { return true; },
                     [](const clap_plugin* plugin) { delete &synthOf(plugin); },
                     [](const clap_plugin* plugin, double rate, std::uint32_t, std::uint32_t) {
                         synthOf(plugin).sampleRate = rate;
                         return true;
                     },
                     [](const clap_plugin*) {},
                     [](const clap_plugin*) { return true; },
                     [](const clap_plugin*) {},
                     [](const clap_plugin* plugin) { synthOf(plugin).key = -1; },
                     process,
                     extension,
                     [](const clap_plugin*) {}};
    return &synth->plugin;
}

const clap_plugin_factory kFactory{
    [](const clap_plugin_factory*) -> std::uint32_t { return 1; },
    [](const clap_plugin_factory*, std::uint32_t index) { return index == 0 ? &kDescriptor : nullptr; }, createPlugin};

bool init(const char*) {
#if defined(EMERICK_TEST_CRASH)
    std::abort();
#elif defined(EMERICK_TEST_HANG)
    std::this_thread::sleep_for(std::chrono::hours(1));
#endif
    // Some real plugins print while they load, as ZynAddSubFX does, and the scanner must survive that.
    std::puts("test sine loaded");
    return true;
}

}  // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry{
    CLAP_VERSION_INIT, init, [] {},
    [](const char* id) -> const void* { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr; }};
