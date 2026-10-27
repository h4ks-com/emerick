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
#include <ranges>
#include <string>

#include "gm.hpp"
#include "gm_sounds.hpp"

namespace {

using emerick::gm::bundledSoundFont;
using emerick::gm::eventAs;
using emerick::gm::Gm;
using emerick::gm::gmOf;
using emerick::gm::kDrumKits;
using emerick::gm::kParams;
using emerick::gm::kState;
using emerick::gm::setParam;
using emerick::gm::wantedSoundFont;

constexpr const char* kSoundFontFile = "GeneralUser-GS.sf2";
constexpr int kDrumChannel = 9;
constexpr int kChannels = 16;

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

void playMidi(fluid_synth_t* synth, const std::array<std::uint8_t, 3>& data) {
    const int channel = data[0] & 0x0F;
    const int first = data[1] & 0x7F;
    const int second = data[2] & 0x7F;
    switch (data[0] & 0xF0) {
        case 0x80:
            fluid_synth_noteoff(synth, channel, first);
            break;
        case 0x90:
            if (second == 0) {
                fluid_synth_noteoff(synth, channel, first);
            } else {
                fluid_synth_noteon(synth, channel, first, second);
            }
            break;
        case 0xA0:
            fluid_synth_key_pressure(synth, channel, first, second);
            break;
        case 0xB0:
            fluid_synth_cc(synth, channel, first, second);
            break;
        case 0xC0:
            fluid_synth_program_change(synth, channel, first);
            break;
        case 0xD0:
            fluid_synth_channel_pressure(synth, channel, first);
            break;
        case 0xE0:
            fluid_synth_pitch_bend(synth, channel, first | (second << 7));
            break;
        default:
            break;
    }
}

void playNote(fluid_synth_t* synth, const clap_event_note& note, bool on) {
    const int channel = std::max<int>(note.channel, 0);
    if (on) {
        fluid_synth_noteon(synth, channel, note.key, velocity(note.velocity));
    } else if (note.key < 0) {
        fluid_synth_all_notes_off(synth, note.channel < 0 ? -1 : channel);
    } else {
        fluid_synth_noteoff(synth, channel, note.key);
    }
}

// Picking a sound sets every melodic channel, as a GM module's own sound button does, until a program change in the
// MIDI picks another.
void sendPrograms(Gm& gm) {
    fluid_synth_t* synth = gm.synth.get();
    const int sound = gm.sound.load(std::memory_order_relaxed);
    for (const int channel : std::views::iota(0, kChannels)) {
        if (channel != kDrumChannel) {
            fluid_synth_bank_select(synth, channel, 0);
            fluid_synth_program_change(synth, channel, sound);
        }
    }
    fluid_synth_program_change(
        synth, kDrumChannel,
        kDrumKits.at(static_cast<std::size_t>(gm.drumKit.load(std::memory_order_relaxed))).program);
}

void play(Gm& gm, const clap_event_header* header) {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) {
        return;
    }
    fluid_synth_t* synth = gm.synth.get();
    switch (header->type) {
        case CLAP_EVENT_NOTE_ON:
            playNote(synth, eventAs<clap_event_note>(header), true);
            break;
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE:
            playNote(synth, eventAs<clap_event_note>(header), false);
            break;
        case CLAP_EVENT_MIDI:
            playMidi(synth, std::to_array(eventAs<clap_event_midi>(header).data));
            break;
        case CLAP_EVENT_PARAM_VALUE:
            setParam(gm, eventAs<clap_event_param_value>(header));
            break;
        default:
            break;
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
