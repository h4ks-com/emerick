// The expansion voices instrument: a CLAP plugin that plays the voices of the Yamaha expansion packs installed in
// Expansion Manager's store, read straight from the store's voice files and wave blobs.
// A thread of its own reads the store, the host's main thread publishes it and decodes a voice's waves when it is
// picked, and the audio thread only plays the banks the main thread published.

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ranges>
#include <span>

#include "voices.hpp"

namespace {

using emerick::voices::ChannelControls;
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
constexpr int kDataEntry = 6;
constexpr int kVolume = 7;
constexpr int kPan = 10;
constexpr int kExpression = 11;
constexpr int kBankLsb = 32;
constexpr int kDataEntryLsb = 38;
constexpr int kSustain = 64;
constexpr int kNrpnLsb = 98;
constexpr int kNrpnMsb = 99;
constexpr int kRpnLsb = 100;
constexpr int kRpnMsb = 101;
constexpr int kAllSoundOff = 120;
constexpr int kResetControllers = 121;
constexpr int kAllNotesOff = 123;
constexpr int kSustainDown = 64;
constexpr int kNoRpn = 127;
constexpr int kPanCenter = 64;
constexpr int kBendCenter = 8192;
constexpr float kControllerTop = 127.0F;
constexpr double kCentsPerSemitone = 100.0;

PlaySettings settingsOf(const Plugin& plugin) {
    return PlaySettings{.outputRate = plugin.sampleRate,
                        .volume = std::pow(10.0F, plugin.volumeDb.load(std::memory_order_relaxed) / 20.0F),
                        .tuneSemitones = plugin.tune.load(std::memory_order_relaxed),
                        .attackSeconds = plugin.attack.load(std::memory_order_relaxed),
                        .releaseSeconds = plugin.release.load(std::memory_order_relaxed)};
}

// We follow General MIDI's level curve for volume and expression, the square of the value, as FluidSynth does.
float levelOf(int value) {
    const float share = static_cast<float>(value) / kControllerTop;
    return share * share;
}

void applyControls(Plugin& plugin, int channel) {
    const ChannelControls& controls = plugin.controls.at(static_cast<std::size_t>(channel));
    plugin.sampler.setMix(channel, levelOf(controls.volume) * levelOf(controls.expression),
                          static_cast<float>(controls.pan - kPanCenter) / kPanCenter);
    const double range = controls.bendSemitones + (controls.bendCents / kCentsPerSemitone);
    plugin.sampler.setBend(channel, static_cast<double>(controls.bend - kBendCenter) / kBendCenter * range);
}

void resetAllControls(Plugin& plugin) {
    plugin.controls.fill({});
    for (const int channel : std::views::iota(0, static_cast<int>(kChannels))) {
        applyControls(plugin, channel);
    }
}

// Reset all controllers keeps volume, pan and the bend range, as General MIDI's recommended practice and FluidSynth do.
void resetControllers(Plugin& plugin, int channel) {
    ChannelControls& controls = plugin.controls.at(static_cast<std::size_t>(channel));
    const ChannelControls kept = controls;
    controls = {};
    controls.volume = kept.volume;
    controls.pan = kept.pan;
    controls.bendSemitones = kept.bendSemitones;
    controls.bendCents = kept.bendCents;
    plugin.sampler.setSustain(channel, false);
    applyControls(plugin, channel);
}

void enterData(ChannelControls& controls, int controller, int value) {
    if (controls.rpnMsb != 0 || controls.rpnLsb != 0) {
        return;
    }
    if (controller == kDataEntry) {
        controls.bendSemitones = value;
    } else {
        controls.bendCents = value;
    }
}

bool activate(const clap_plugin* plugin, double sampleRate, std::uint32_t /*unused*/, std::uint32_t /*unused*/) {
    Plugin& voices = pluginOf(plugin);
    emerick::voices::publishLoadedStore(voices);
    emerick::voices::reloadStore(voices);
    emerick::voices::buildBank(voices, voices.selected.load(std::memory_order_relaxed));
    voices.sampleRate = sampleRate;
    voices.sampler.silence();
    resetAllControls(voices);
    voices.playingBank = nullptr;
    voices.active = true;
    return true;
}

void onMainThread(const clap_plugin* plugin) {
    Plugin& voices = pluginOf(plugin);
    emerick::voices::publishLoadedStore(voices);
    emerick::voices::buildBank(voices, voices.bankWanted.exchange(-1, std::memory_order_relaxed));
}

int velocityOf(double clapVelocity) { return std::clamp(static_cast<int>(std::lround(clapVelocity * 127.0)), 1, 127); }

int channelOf(std::int16_t channel) { return std::clamp<int>(channel, 0, static_cast<int>(kChannels) - 1); }

const emerick::voices::Bank* updatePlayingBank(Plugin& plugin) {
    const int selected = plugin.selected.load(std::memory_order_relaxed);
    if (selected < 0) {
        plugin.playingBank = nullptr;
    } else if (const auto* bank = emerick::voices::playableBank(plugin, selected)) {
        plugin.playingBank = bank;
    }
    return plugin.playingBank;
}

void startNote(Plugin& plugin, int key, int velocity, int channel) {
    if (const emerick::voices::Bank* bank = updatePlayingBank(plugin)) {
        plugin.sampler.noteOn(*bank, key, velocity, channel, settingsOf(plugin));
    }
}

void stopNote(Plugin& plugin, int key, int channel) {
    plugin.sampler.noteOff(updatePlayingBank(plugin), key, channel, settingsOf(plugin));
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
        updatePlayingBank(plugin);
        emerick::voices::selectVoice(plugin, voice, true);
        plugin.voiceChanged = true;
    }
}

void controlChange(Plugin& plugin, int channel, int controller, int value) {
    const auto index = static_cast<std::size_t>(channel);
    ChannelControls& controls = plugin.controls.at(index);
    switch (controller) {
        case kBankMsb:
            plugin.bankMsb[index] = value;
            break;
        case kBankLsb:
            plugin.bankLsb[index] = value;
            break;
        case kVolume:
            controls.volume = value;
            applyControls(plugin, channel);
            break;
        case kExpression:
            controls.expression = value;
            applyControls(plugin, channel);
            break;
        case kPan:
            controls.pan = value;
            applyControls(plugin, channel);
            break;
        case kRpnMsb:
            controls.rpnMsb = value;
            break;
        case kRpnLsb:
            controls.rpnLsb = value;
            break;
        case kNrpnMsb:
        case kNrpnLsb:
            controls.rpnMsb = kNoRpn;
            controls.rpnLsb = kNoRpn;
            break;
        case kDataEntry:
        case kDataEntryLsb:
            enterData(controls, controller, value);
            applyControls(plugin, channel);
            break;
        case kSustain:
            plugin.sampler.setSustain(channel, value >= kSustainDown);
            break;
        case kAllSoundOff:
            plugin.sampler.silence();
            break;
        case kResetControllers:
            resetControllers(plugin, channel);
            break;
        case kAllNotesOff:
            plugin.sampler.releaseAll();
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
            stopNote(plugin, first, channel);
            break;
        case 0x90:
            if (second == 0) {
                stopNote(plugin, first, channel);
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
        case 0xE0:
            plugin.controls.at(static_cast<std::size_t>(channel)).bend = first | (second << 7);
            applyControls(plugin, channel);
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
        plugin.sampler.releaseAll();
    } else {
        stopNote(plugin, note.key, channel);
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
                      [](const clap_plugin* plugin) {
                          emerick::voices::dropStoreLoad(pluginOf(plugin));
                          std::unique_ptr<Plugin>{&pluginOf(plugin)}.reset();
                      },
                      activate,
                      [](const clap_plugin* plugin) { pluginOf(plugin).active = false; },
                      [](const clap_plugin*) { return true; },
                      [](const clap_plugin*) {},
                      [](const clap_plugin* plugin) {
                          pluginOf(plugin).sampler.silence();
                          resetAllControls(pluginOf(plugin));
                      },
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
