#pragma once

#include <sndfile.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "emerick/engine/engine.hpp"

namespace emerick::engine::test {

inline constexpr double kRate = 48000.0;
inline constexpr Frames kBlock = 64;
inline const std::string kPort1 = "Digital Keyboard Port 1";
inline const std::string kPort2 = "Digital Keyboard Port 2";

inline std::vector<AudioDeviceInfo> rigDevices() {
    return {{"interface", "Interface", 2, 2}, {"keyboard", "Digital Keyboard", 2, 2}};
}

// An engine on the offline audio backend and virtual MIDI ports, so every test runs without hardware.
struct Rig {
    explicit Rig(Frames blockFrames = kBlock, std::int64_t startTimeNs = 0, Frames recordingDelay = 0)
        : audioOwner(std::make_unique<OfflineBackend>(rigDevices(), startTimeNs)),
          midiOwner(std::make_unique<VirtualMidiBackend>(std::vector{kPort1, kPort2}, std::vector{kPort1, kPort2})),
          audio(*audioOwner),
          midi(*midiOwner),
          engine(std::move(audioOwner), std::move(midiOwner), {kRate, blockFrames, recordingDelay}) {}

    std::unique_ptr<OfflineBackend> audioOwner;
    std::unique_ptr<VirtualMidiBackend> midiOwner;
    OfflineBackend& audio;
    VirtualMidiBackend& midi;
    Engine engine;
};

template <class Predicate>
bool waitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

inline Frames frameOf(const SentMidi& message) {
    return std::llround(static_cast<double>(message.timestampNs) * kRate / 1e9);
}

inline void writeWav(const std::filesystem::path& path, const std::vector<float>& samples, double rate = kRate) {
    SF_INFO info{.samplerate = static_cast<int>(rate), .channels = 1, .format = SF_FORMAT_WAV | SF_FORMAT_FLOAT};
    SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &info);
    REQUIRE(file != nullptr);
    sf_writef_float(file, samples.data(), static_cast<sf_count_t>(samples.size()));
    sf_close(file);
}

inline void playOnceReadAhead(Rig& rig) {
    rig.engine.locate(0);
    rig.audio.process(kBlock);
    REQUIRE(waitFor([&] { return rig.engine.readAheadReady(); }));
    rig.engine.play();
}

struct TempFolder {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("emerick-engine-test-" + std::to_string(std::random_device{}()));
    TempFolder() { std::filesystem::create_directories(path); }
    ~TempFolder() { std::filesystem::remove_all(path); }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
    TempFolder(TempFolder&&) = delete;
    TempFolder& operator=(TempFolder&&) = delete;
};

}  // namespace emerick::engine::test
