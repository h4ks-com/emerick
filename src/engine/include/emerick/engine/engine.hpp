#pragma once

#include <memory>
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

float fadeGain(Frames local, Frames length, Frames fadeIn, Frames fadeOut);

// We call every method from the UI thread, and the audio thread runs only the backend callback.
class Engine {
public:
    struct Options {
        double sampleRate = 48000.0;
        Frames blockFrames = 256;
    };

    Engine(std::unique_ptr<AudioBackend> audio, std::unique_ptr<MidiBackend> midi, Options options);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    // We swap the session in at the next block and list every clip we skip in sessionErrors.
    void setSession(Session session);

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

    [[nodiscard]] AudioBackend& audio();
    [[nodiscard]] MidiBackend& midi();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace emerick::engine
