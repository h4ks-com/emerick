// The General MIDI instrument: a CLAP plugin that plays all 16 MIDI channels through FluidSynth and a SoundFont.
// An empty SoundFont path plays the GeneralUser GS bank shipped inside the plugin bundle.

#include <clap/clap.h>
#include <fluidsynth.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "gm.hpp"

namespace {

using emerick::gm::bundledSoundFont;
using emerick::gm::eventAs;
using emerick::gm::Gm;
using emerick::gm::gmOf;
using emerick::gm::kParams;
using emerick::gm::kState;
using emerick::gm::playMidi;
using emerick::gm::sendPrograms;
using emerick::gm::setParam;
using emerick::gm::wantedSoundFont;

constexpr const char* kSoundFontFile = "GeneralUser-GS.sf2";

bool activate(const clap_plugin* plugin, double sampleRate, std::uint32_t /*unused*/, std::uint32_t /*unused*/) {
    Gm& gm = gmOf(plugin);
    const std::filesystem::path soundFont = wantedSoundFont(gm);
    if (!gm.synth || sampleRate != gm.loadedRate || soundFont != gm.loadedSoundFont) {
        gm.synth.reset();
        gm.settings.reset(new_fluid_settings());
        fluid_settings_setnum(gm.settings.get(), "synth.sample-rate", sampleRate);
        // FluidSynth's default gain of 0.2 plays a single piano note near -27 dBFS, far below other instruments.
        fluid_settings_setnum(gm.settings.get(), "synth.gain", 0.5);
        // Only the host's audio thread plays the synth, and the main thread touches it only while it is inactive.
        fluid_settings_setint(gm.settings.get(), "synth.threadsafe-api", 0);
        gm.synth.reset(new_fluid_synth(gm.settings.get()));
        if (!gm.synth || fluid_synth_sfload(gm.synth.get(), soundFont.string().c_str(), 1) == FLUID_FAILED) {
            gm.synth.reset();
            gm.loadedSoundFont.clear();
            return false;
        }
        gm.loadedRate = sampleRate;
        gm.loadedSoundFont = soundFont;
    }
    gm.programsPending.store(true, std::memory_order_relaxed);
    gm.active = true;
    return true;
}

int velocity(double clapVelocity) { return std::clamp(static_cast<int>(std::lround(clapVelocity * 127.0)), 1, 127); }

void playNote(Gm& gm, const clap_event_note& note, bool on) {
    fluid_synth_t* synth = gm.synth.get();
    const int channel = std::max<int>(note.channel, 0);
    if (note.key < 0) {
        if (!on) {
            fluid_synth_all_notes_off(synth, note.channel < 0 ? -1 : channel);
        }
        return;
    }
    const int played = emerick::gm::drumNote(gm, channel, note.key);
    if (played < 0) {
        return;
    }
    if (!on) {
        fluid_synth_noteoff(synth, channel, played);
    } else if (note.key <= gm.highestNote.load(std::memory_order_relaxed)) {
        fluid_synth_noteon(synth, channel, played, velocity(note.velocity));
    }
}

void play(Gm& gm, const clap_event_header* header) {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) {
        return;
    }
    switch (header->type) {
        case CLAP_EVENT_NOTE_ON:
            playNote(gm, eventAs<clap_event_note>(header), true);
            break;
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE:
            playNote(gm, eventAs<clap_event_note>(header), false);
            break;
        case CLAP_EVENT_MIDI:
            playMidi(gm, std::to_array(eventAs<clap_event_midi>(header).data));
            break;
        case CLAP_EVENT_PARAM_VALUE:
            setParam(gm, eventAs<clap_event_param_value>(header));
            break;
        default:
            break;
    }
}

void reportParam(const clap_output_events* out, clap_id id, int value) {
    const clap_event_param_value event{.header = {.size = sizeof(clap_event_param_value),
                                                  .time = 0,
                                                  .space_id = CLAP_CORE_EVENT_SPACE_ID,
                                                  .type = CLAP_EVENT_PARAM_VALUE,
                                                  .flags = 0},
                                       .param_id = id,
                                       .cookie = nullptr,
                                       .note_id = -1,
                                       .port_index = -1,
                                       .channel = -1,
                                       .key = -1,
                                       .value = static_cast<double>(value)};
    out->try_push(out, &event.header);
}

void reportPrograms(Gm& gm, const clap_output_events* out) {
    if (std::exchange(gm.soundChanged, false)) {
        reportParam(out, emerick::gm::kSoundParam, gm.sound.load(std::memory_order_relaxed));
    }
    if (std::exchange(gm.drumKitChanged, false)) {
        reportParam(out, emerick::gm::kDrumKitParam, gm.drumKit.load(std::memory_order_relaxed));
    }
}

clap_process_status process(const clap_plugin* plugin, const clap_process* process) {
    Gm& gm = gmOf(plugin);
    fluid_synth_t* synth = gm.synth.get();
    float** outputs = process->audio_outputs[0].data32;
    const auto render = [&](std::uint32_t from, std::uint32_t to) {
        if (to > from) {
            fluid_synth_write_float(synth, static_cast<int>(to - from), outputs[0], static_cast<int>(from), 1,
                                    outputs[1], static_cast<int>(from), 1);
        }
    };
    std::uint32_t rendered = 0;
    const std::uint32_t events = process->in_events->size(process->in_events);
    for (std::uint32_t index = 0; index < events; ++index) {
        const clap_event_header* header = process->in_events->get(process->in_events, index);
        const std::uint32_t time = std::clamp(header->time, rendered, process->frames_count);
        render(rendered, time);
        rendered = time;
        if (gm.programsPending.exchange(false, std::memory_order_relaxed)) {
            sendPrograms(gm);
        }
        play(gm, header);
    }
    if (gm.programsPending.exchange(false, std::memory_order_relaxed)) {
        sendPrograms(gm);
    }
    render(rendered, process->frames_count);
    reportPrograms(gm, process->out_events);
    return CLAP_PROCESS_CONTINUE;
}

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

constexpr std::array<const char*, 4> kFeatures{CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SAMPLER,
                                               CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor kDescriptor{CLAP_VERSION_INIT,
                                         "org.emerick.gm",
                                         "General MIDI",
                                         "Emerick",
                                         "",
                                         "",
                                         "",
                                         "1.0.0",
                                         "The 128 General MIDI sounds and drums on channel 10, played by FluidSynth",
                                         kFeatures.data()};

const clap_plugin* createPlugin(const clap_plugin_factory* /*unused*/, const clap_host* host, const char* id) {
    if (std::strcmp(id, kDescriptor.id) != 0) {
        return nullptr;
    }
    auto gm = std::make_unique<Gm>();
    gm->host = host;
    gm->plugin = {&kDescriptor,
                  gm.get(),
                  [](const clap_plugin*) { return true; },
                  [](const clap_plugin* plugin) { std::unique_ptr<Gm>{&gmOf(plugin)}.reset(); },
                  activate,
                  [](const clap_plugin* plugin) { gmOf(plugin).active = false; },
                  [](const clap_plugin*) { return true; },
                  [](const clap_plugin*) {},
                  [](const clap_plugin* plugin) { fluid_synth_all_sounds_off(gmOf(plugin).synth.get(), -1); },
                  process,
                  extension,
                  [](const clap_plugin*) {}};
    return &gm.release()->plugin;
}

const clap_plugin_factory kFactory{
    [](const clap_plugin_factory*) -> std::uint32_t { return 1; },
    [](const clap_plugin_factory*, std::uint32_t index) { return index == 0 ? &kDescriptor : nullptr; }, createPlugin};

bool init(const char* pluginPath) {
#ifdef __APPLE__
    bundledSoundFont() = std::filesystem::path{pluginPath} / "Contents/Resources" / kSoundFontFile;
#else
    bundledSoundFont() = std::filesystem::path{pluginPath}.parent_path() / kSoundFontFile;
#endif
    // FluidSynth warns about a missing bank from inside a note or program change, on the audio thread.
    for (const int level : {FLUID_WARN, FLUID_INFO, FLUID_DBG}) {
        fluid_set_log_function(level, nullptr, nullptr);
    }
    return true;
}

void deinit() {}

const void* factory(const char* id) {
    return std::strcmp(id, std::data(CLAP_PLUGIN_FACTORY_ID)) == 0 ? &kFactory : nullptr;
}

}  // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry{CLAP_VERSION_INIT, init, deinit, factory};
