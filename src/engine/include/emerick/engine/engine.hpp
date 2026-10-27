#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/engine/audio_backend.hpp"
#include "emerick/engine/midi_backend.hpp"
#include "emerick/engine/session.hpp"

namespace emerick::engine {

struct TransportPosition {
    Frames frames = 0;
    Ticks ticks = 0;
};

// Meters show at most this many channels; the last one also shows every channel beyond it.
inline constexpr std::size_t kMeterChannels = 2;

struct LevelReading {
    std::string trackId;
    std::vector<float> peaks;  // the loudest sample per metered channel since the last reading
    bool clipped = false;      // a sample reached full scale since the last reading
};

float fadeGain(Frames local, Frames length, Frames fadeIn, Frames fadeOut, core::FadeCurve inCurve = {},
               core::FadeCurve outCurve = {});

struct StereoGains {
    float left = 1.0F;
    float right = 1.0F;
};

StereoGains panGains(float pan, PanLaw law);

struct GainReduction {
    std::string insertId;
    float db = 0.0F;  // zero or below
};

inline constexpr auto kStreamStall = std::chrono::seconds{2};

// We call every method from the UI thread, and the audio thread runs only the backend callback.
class Engine {
public:
    struct Options {
        double sampleRate = 48000.0;
        Frames blockFrames = 256;
        // Frames an audio take skips beyond the round trip the device reports, for devices that report it wrong.
        Frames recordingDelay = 0;
    };

    Engine(std::unique_ptr<AudioBackend> audio, std::unique_ptr<MidiBackend> midi, Options options);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine& operator=(Engine&&) = delete;

    // We swap the session in at the next block and list every clip we skip in sessionErrors. A device that cannot
    // run at the engine's sample rate throws before the open stream changes. We open the devices the outputs route
    // to and the inputs of armed and monitored tracks, and only while the transport runs or a track listens live.
    void setSession(Session session);
    // Closes the stream once the transport stopped and no track listens live, so other apps have the devices to
    // themselves.
    void releaseIdleStream();
    // A warm engine opens its stream now and keeps it open while stopped, so Play starts without opening devices.
    void keepWarm(bool value);

    // We list devices once at startup and again on refreshDevices, so reading these never asks the hardware.
    [[nodiscard]] const std::vector<AudioDeviceInfo>& audioDevices() const;
    [[nodiscard]] const std::vector<MidiPortInfo>& midiPorts() const;
    // True once a backend reported a device change after the last listing.
    [[nodiscard]] bool devicesChanged() const;
    void refreshDevices();

    void play();
    void stop();
    void locate(Ticks ticks);
    // Queues bytes on an output port the session uses, ahead of whatever the next play or locate sends, so a chase
    // reaches the device before Start. A port the session does not use drops them; a message over 64 KiB throws.
    void sendMidi(std::string_view portId, std::span<const std::uint8_t> bytes);
    // Plays a channel message on the track's instrument while the track is audible. A note it starts ends with the
    // track's other notes on stop and panic.
    void sendInstrumentMidi(std::string_view trackId, std::span<const std::uint8_t> bytes);
    // Sends Stop to the clock ports, releases the notes the tracks hold, and sends sustain off, all sound off and all
    // notes off on every channel of every output port the session uses. It works while stopped.
    void panic();
    void setRecording(bool on);
    void setMute(std::string_view trackId, bool mute);
    void setSolo(std::string_view trackId, bool solo);
    void setArmed(std::string_view trackId, bool armed);
    void setGain(std::string_view trackId, float gain);
    void setPan(std::string_view trackId, float pan);

    [[nodiscard]] TransportPosition position() const;
    [[nodiscard]] bool playing() const;
    [[nodiscard]] const StreamLayout& layout() const;
    [[nodiscard]] bool streamOpen() const;
    // Returns true once when the open stream made no callback for kStreamStall while the transport runs or a track
    // listens live. We then close the stream and stop the transport, so every take in flight finishes.
    bool watchStream(std::chrono::steady_clock::time_point now);
    // True from a stall until the stream calls back again.
    [[nodiscard]] bool streamStalled() const;
    [[nodiscard]] const std::vector<SessionError>& sessionErrors() const;
    // True once every locate has reached the audio thread and every clip has its read-ahead filled from there.
    [[nodiscard]] bool readAheadReady() const;
    // True once the sender has sent every MIDI message the audio thread queued.
    [[nodiscard]] bool midiFlushed() const;
    // Returns the takes finished since the last call.
    std::vector<RecordedTake> takeRecordings();
    // Returns what every take in progress gained since the last call.
    std::vector<TakeProgress> takeProgress();
    // Reads and resets the input level of every audio track with an input.
    std::vector<LevelReading> takeInputLevels();
    // Reads and resets the deepest gain reduction of every compressor insert.
    std::vector<GainReduction> takeGainReductions();
    // Reads and resets the post-fader output level of every track with an audio output.
    std::vector<LevelReading> takeOutputLevels();

    [[nodiscard]] AudioBackend& audio();
    [[nodiscard]] MidiBackend& midi();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace emerick::engine
