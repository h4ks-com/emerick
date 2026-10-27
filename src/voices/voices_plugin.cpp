// The expansion voices instrument: a CLAP plugin that plays the voices of the Yamaha expansion packs installed in
// Expansion Manager's store, read straight from the store's voice files and wave blobs.
// The host's main thread reads the store and decodes a voice's waves when it is picked; the audio thread only plays
// the banks the main thread published.

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>

#include "voices.hpp"

namespace {

using emerick::voices::eventAs;
using emerick::voices::kChannels;
using emerick::voices::kParams;
using emerick::voices::kState;
using emerick::voices::kVoiceParam;
using emerick::voices::PlaySettings;
using emerick::voices::Plugin;
using emerick::voices::pluginOf;
using emerick::voices::VoiceAddress;

constexpr int kBankMsb = 0;
constexpr int kBankLsb = 32;
constexpr int kSustain = 64;
constexpr int kAllSoundOff = 120;
constexpr int kAllNotesOff = 123;
constexpr int kSustainDown = 64;

PlaySettings settingsOf(const Plugin& plugin) {
    return PlaySettings{.outputRate = plugin.sampleRate,
                        .volume = std::pow(10.0F, plugin.volumeDb.load(std::memory_order_relaxed) / 20.0F),
                        .tuneSemitones = plugin.tune.load(std::memory_order_relaxed),
                        .attackSeconds = plugin.attack.load(std::memory_order_relaxed),
                        .releaseSeconds = plugin.release.load(std::memory_order_relaxed)};
}

bool activate(const clap_plugin* plugin, double sampleRate, std::uint32_t /*unused*/, std::uint32_t /*unused*/) {
    Plugin& voices = pluginOf(plugin);
    emerick::voices::reloadStore(voices);
    emerick::voices::buildBank(voices, voices.selected.load(std::memory_order_relaxed));
    voices.sampleRate = sampleRate;
    voices.sampler.silence();
    voices.active = true;
    return true;
}

void onMainThread(const clap_plugin* plugin) {
    Plugin& voices = pluginOf(plugin);
    emerick::voices::buildBank(voices, voices.bankWanted.exchange(-1, std::memory_order_relaxed));
}

int velocityOf(double clapVelocity) { return std::clamp(static_cast<int>(std::lround(clapVelocity * 127.0)), 1, 127); }

int channelOf(std::int16_t channel) { return std::clamp<int>(channel, 0, static_cast<int>(kChannels) - 1); }

const emerick::voices::Bank* currentBank(const Plugin& plugin) {
    const int selected = plugin.selected.load(std::memory_order_relaxed);
    return selected >= 0 ? plugin.banks[static_cast<std::size_t>(selected)].load(std::memory_order_acquire) : nullptr;
}

void startNote(Plugin& plugin, int key, int velocity, int channel) {
    if (const emerick::voices::Bank* bank = currentBank(plugin)) {
        plugin.sampler.noteOn(*bank, key, velocity, channel, settingsOf(plugin));
    }
}

// With Follow program changes on, a program change picks the voice at the channel's bank and that program, or none.
void changeProgram(Plugin& plugin, int channel, int program) {
    if (!plugin.followProgramChanges.load(std::memory_order_relaxed)) {
        return;
    }
    const auto index = static_cast<std::size_t>(channel);
    const VoiceAddress address{.msb = plugin.bankMsb[index], .lsb = plugin.bankLsb[index], .program = program};
    const auto found = std::ranges::lower_bound(plugin.addresses, address, {}, &std::pair<VoiceAddress, int>::first);
    const int voice = found != plugin.addresses.end() && found->first == address ? found->second : -1;
    if (voice != plugin.selected.load(std::memory_order_relaxed)) {
        emerick::voices::selectVoice(plugin, voice, true);
        plugin.voiceChanged = true;
    }
}

void controlChange(Plugin& plugin, int channel, int controller, int value) {
    const auto index = static_cast<std::size_t>(channel);
    switch (controller) {
        case kBankMsb:
            plugin.bankMsb[index] = value;
            break;
        case kBankLsb:
            plugin.bankLsb[index] = value;
            break;
        case kSustain:
            plugin.sampler.setSustain(channel, value >= kSustainDown, settingsOf(plugin));
            break;
        case kAllSoundOff:
            plugin.sampler.silence();
            break;
        case kAllNotesOff:
            plugin.sampler.releaseAll(settingsOf(plugin));
            break;
        default:
            break;
    }
}

void playMidi(Plugin& plugin, const std::array<std::uint8_t, 3>& data) {
    const int channel = data[0] & 0x0F;
    const int first = data[1] & 0x7F;
    const int second = data[2] & 0x7F;
    switch (data[0] & 0xF0) {
        case 0x80:
            plugin.sampler.noteOff(first, channel, settingsOf(plugin));
            break;
        case 0x90:
            if (second == 0) {
                plugin.sampler.noteOff(first, channel, settingsOf(plugin));
            } else {
                startNote(plugin, first, second, channel);
            }
            break;
        case 0xB0:
            controlChange(plugin, channel, first, second);
            break;
        case 0xC0:
            changeProgram(plugin, channel, first);
            break;
        default:
            break;
    }
}

void playNote(Plugin& plugin, const clap_event_note& note, bool on) {
    const int channel = channelOf(note.channel);
    if (on) {
        startNote(plugin, note.key, velocityOf(note.velocity), channel);
    } else if (note.key < 0) {
        plugin.sampler.releaseAll(settingsOf(plugin));
    } else {
        plugin.sampler.noteOff(note.key, channel, settingsOf(plugin));
    }
}

void play(Plugin& plugin, const clap_event_header* header) {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) {
        return;
    }
    switch (header->type) {
        case CLAP_EVENT_NOTE_ON:
            playNote(plugin, eventAs<clap_event_note>(header), true);
            break;
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE:
            playNote(plugin, eventAs<clap_event_note>(header), false);
            break;
        case CLAP_EVENT_MIDI:
            playMidi(plugin, std::to_array(eventAs<clap_event_midi>(header).data));
            break;
        case CLAP_EVENT_PARAM_VALUE:
            emerick::voices::setParam(plugin, eventAs<clap_event_param_value>(header), true);
            break;
        default:
            break;
    }
}

// Tells the host the Voice param moved, when a program change picked another voice.
void reportVoice(Plugin& plugin, const clap_output_events* out) {
    if (!plugin.voiceChanged) {
        return;
    }
    plugin.voiceChanged = false;
    clap_event_param_value event{};
    event.header = {.size = sizeof(event),
                    .time = 0,
                    .space_id = CLAP_CORE_EVENT_SPACE_ID,
                    .type = CLAP_EVENT_PARAM_VALUE,
                    .flags = 0};
    event.param_id = kVoiceParam;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = plugin.selected.load(std::memory_order_relaxed) + 1;
    out->try_push(out, &event.header);
}

clap_process_status process(const clap_plugin* plugin, const clap_process* process) {
    Plugin& voices = pluginOf(plugin);
    const std::span<float* const> outputs(process->audio_outputs[0].data32, 2);
    const std::span<float> left(outputs[0], process->frames_count);
    const std::span<float> right(outputs[1], process->frames_count);
    std::ranges::fill(left, 0.0F);
    std::ranges::fill(right, 0.0F);
    std::uint32_t rendered = 0;
    const auto render = [&](std::uint32_t to) {
        voices.sampler.render(left.subspan(rendered, to - rendered), right.subspan(rendered, to - rendered));
        rendered = to;
    };
    const std::uint32_t events = process->in_events->size(process->in_events);
    for (std::uint32_t index = 0; index < events; ++index) {
        const clap_event_header* header = process->in_events->get(process->in_events, index);
        render(std::clamp(header->time, rendered, process->frames_count));
        play(voices, header);
    }
    render(process->frames_count);
    reportVoice(voices, process->out_events);
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
                                         "org.emerick.voices",
                                         "Expansion Voices",
                                         "Emerick",
                                         "",
                                         "",
                                         "",
                                         "1.0.0",
                                         "The voices of the Yamaha expansion packs installed in Expansion Manager",
                                         kFeatures.data()};

const clap_plugin* createPlugin(const clap_plugin_factory* /*unused*/, const clap_host* host, const char* id) {
    if (std::strcmp(id, kDescriptor.id) != 0) {
        return nullptr;
    }
    auto voices = std::make_unique<Plugin>();
    voices->host = host;
    voices->plugin = {&kDescriptor,
                      voices.get(),
                      [](const clap_plugin*) { return true; },
                      [](const clap_plugin* plugin) { std::unique_ptr<Plugin>{&pluginOf(plugin)}.reset(); },
                      activate,
                      [](const clap_plugin* plugin) { pluginOf(plugin).active = false; },
                      [](const clap_plugin*) { return true; },
                      [](const clap_plugin*) {},
                      [](const clap_plugin* plugin) { pluginOf(plugin).sampler.silence(); },
                      process,
                      extension,
                      onMainThread};
    return &voices.release()->plugin;
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
