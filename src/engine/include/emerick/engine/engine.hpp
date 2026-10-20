#pragma once

#include <memory>
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

struct InputLevel {
    std::string trackId;
    float peak = 0.0F;     // the loudest input sample since the last reading
    bool clipped = false;  // a sample reached full scale since the last reading
};

float fadeGain(Frames local, Frames length, Frames fadeIn, Frames fadeOut);

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
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    // We swap the session in at the next block and list every clip we skip in sessionErrors. A device that cannot
    // run at the engine's sample rate throws before the open stream changes. We open the devices the outputs route
    // to and the inputs of armed and monitored tracks, and only while the transport runs or a track listens live.
    void setSession(Session session);
    // Closes the stream once the transport stopped and no track listens live, so other apps have the devices to
    // themselves.
    void releaseIdleStream();

    // We list devices once at startup and again on refreshDevices, so reading these never asks the hardware.
    [[nodiscard]] const std::vector<AudioDeviceInfo>& audioDevices() const;
    [[nodiscard]] const std::vector<MidiPortInfo>& midiPorts() const;
    // True once a backend reported a device change after the last listing.
    [[nodiscard]] bool devicesChanged() const;
    void refreshDevices();

    void play();
    void stop();
    void locate(Ticks ticks);
    void setRecording(bool on);
    void setMute(std::string_view trackId, bool mute);
    void setSolo(std::string_view trackId, bool solo);
    void setArmed(std::string_view trackId, bool armed);
    void setGain(std::string_view trackId, float gain);

    [[nodiscard]] TransportPosition position() const;
    [[nodiscard]] bool playing() const;
    [[nodiscard]] const StreamLayout& layout() const;
    [[nodiscard]] const std::vector<SessionError>& sessionErrors() const;
    // True once every locate has reached the audio thread and every clip has its read-ahead filled from there.
    [[nodiscard]] bool readAheadReady() const;
    // True once the sender has sent every MIDI message the audio thread queued.
    [[nodiscard]] bool midiFlushed() const;
    // Returns the takes finished since the last call.
    std::vector<RecordedTake> takeRecordings();
    // Reads and resets the input level of every audio track with an input.
    std::vector<InputLevel> takeInputLevels();

    [[nodiscard]] AudioBackend& audio();
    [[nodiscard]] MidiBackend& midi();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace emerick::engine
