// How the General MIDI instrument plays MIDI messages and its Sound and Drum kit params on FluidSynth.

#include <fluidsynth.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>

#include "gm.hpp"
#include "gm_sounds.hpp"

namespace emerick::gm {

namespace {

constexpr int kDrumChannel = 9;
constexpr int kBankSelect = 0;
constexpr int kDrumBank = 128;

bool isDrumChannel(const Gm& gm, int channel) { return gm.drumChannels.at(static_cast<std::size_t>(channel)); }

void setDrums(Gm& gm, int channel, bool drums) {
    fluid_synth_set_channel_type(gm.synth.get(), channel, drums ? CHANNEL_TYPE_DRUM : CHANNEL_TYPE_MELODIC);
    gm.drumChannels.at(static_cast<std::size_t>(channel)) = drums;
}

void selectBank(Gm& gm, int channel, int msb) {
    if (isDrumBank(msb)) {
        setDrums(gm, channel, true);
    } else if (channel != kDrumChannel) {
        setDrums(gm, channel, false);
    }
}

bool follows(const Gm& gm, int channel) {
    return gm.followProgramChanges.load(std::memory_order_relaxed) || isDrumChannel(gm, channel);
}

int pickedKit(const Gm& gm) {
    return kDrumKits.at(static_cast<std::size_t>(gm.drumKit.load(std::memory_order_relaxed))).program;
}

// A program change becomes the Sound param on a melodic channel and the Drum kit param on a drum channel when it
// names a kit, so the host shows what the MIDI picked.
void followProgram(Gm& gm, int channel, int program) {
    fluid_preset_t* preset = fluid_synth_get_channel_preset(gm.synth.get(), channel);
    if (preset == nullptr) {
        return;
    }
    if (fluid_preset_get_banknum(preset) != kDrumBank) {
        if (program != gm.sound.load(std::memory_order_relaxed)) {
            gm.sound.store(program, std::memory_order_relaxed);
            gm.soundChanged = true;
        }
        return;
    }
    const DrumKit* kit = drumKitOf(program);
    const auto index = static_cast<int>(kit - kDrumKits.data());
    if (kit != nullptr && index != gm.drumKit.load(std::memory_order_relaxed)) {
        gm.drumKit.store(index, std::memory_order_relaxed);
        gm.drumKitChanged = true;
    }
}

// Yamaha styles pick kits by numbers General MIDI lacks, so such a drum channel plays the Drum kit param's kit.
void changeProgram(Gm& gm, int channel, int program) {
    const bool unknownKit = isDrumChannel(gm, channel) && drumKitOf(program) == nullptr;
    fluid_synth_program_change(gm.synth.get(), channel, unknownKit ? pickedKit(gm) : program);
    if (!unknownKit) {
        followProgram(gm, channel, program);
    }
}

void noteOff(Gm& gm, int channel, int note) {
    if (const int played = drumNote(gm, channel, note); played >= 0) {
        fluid_synth_noteoff(gm.synth.get(), channel, played);
    }
}

}  // namespace

int drumNote(const Gm& gm, int channel, int note) {
    if (!isDrumChannel(gm, channel) || note >= kNotes) {
        return note;
    }
    const int played = note + gm.drumNoteOffsets.at(static_cast<std::size_t>(note)).load(std::memory_order_relaxed);
    return played >= 0 ? played : -1;
}

void playMidi(Gm& gm, const std::array<std::uint8_t, 3>& data) {
    fluid_synth_t* synth = gm.synth.get();
    const int channel = data[0] & 0x0F;
    const int first = data[1] & 0x7F;
    const int second = data[2] & 0x7F;
    switch (data[0] & 0xF0) {
        case 0x80:
            noteOff(gm, channel, first);
            break;
        case 0x90:
            if (second == 0) {
                noteOff(gm, channel, first);
            } else if (const int played = drumNote(gm, channel, first);
                       played >= 0 && first <= gm.highestNote.load(std::memory_order_relaxed)) {
                fluid_synth_noteon(synth, channel, played, second);
            }
            break;
        case 0xA0:
            fluid_synth_key_pressure(synth, channel, first, second);
            break;
        case 0xB0:
            if (first == kBankSelect) {
                if (!gm.followProgramChanges.load(std::memory_order_relaxed) && !isDrumBank(second)) {
                    break;
                }
                selectBank(gm, channel, second);
            }
            fluid_synth_cc(synth, channel, first, second);
            break;
        case 0xC0:
            if (follows(gm, channel)) {
                changeProgram(gm, channel, first);
            }
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

// Picking a sound sets every melodic channel, as a GM module's own sound button does, until a program change in the
// MIDI picks another. A channel a drum bank select made a drum channel stays one and plays the picked kit.
void sendPrograms(Gm& gm) {
    fluid_synth_t* synth = gm.synth.get();
    gm.drumChannels.at(static_cast<std::size_t>(kDrumChannel)) = true;
    for (const int channel : std::views::iota(0, kChannels)) {
        const bool drums = isDrumChannel(gm, channel);
        setDrums(gm, channel, drums);
        if (drums) {
            fluid_synth_program_change(synth, channel, pickedKit(gm));
        } else {
            fluid_synth_bank_select(synth, channel, 0);
            fluid_synth_program_change(synth, channel, gm.sound.load(std::memory_order_relaxed));
        }
    }
}

}  // namespace emerick::gm
