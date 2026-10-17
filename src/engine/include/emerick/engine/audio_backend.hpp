#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "emerick/core/time.hpp"

namespace emerick::engine {

using core::Frames;

struct AudioDeviceInfo {
    std::string id;
    std::string name;
    int inputChannels = 0;
    int outputChannels = 0;
};

struct DeviceChannels {
    std::string id;
    int firstInput = 0;
    int inputs = 0;
    int firstOutput = 0;
    int outputs = 0;
};

// One stream concatenates the channels of every device it opened, in request order.
struct StreamLayout {
    double sampleRate = 48000.0;
    Frames maxFrames = 256;
    std::vector<DeviceChannels> devices;
    int inputChannels = 0;
    int outputChannels = 0;

    [[nodiscard]] std::optional<int> inputIndex(std::string_view deviceId, int channel) const;
    [[nodiscard]] std::optional<int> outputIndex(std::string_view deviceId, int channel) const;
};

struct StreamRequest {
    std::vector<std::string> deviceIds;  // empty opens the default output device
    double sampleRate = 48000.0;
    Frames blockFrames = 256;
};

struct AudioBlock {
    Frames frames = 0;
    std::int64_t hostTimeNs = 0;  // steady clock time of the block's first frame
    std::span<const float* const> inputs;
    std::span<float* const> outputs;
};

using AudioCallback = std::function<void(const AudioBlock&)>;

class AudioBackend {
public:
    AudioBackend() = default;
    AudioBackend(const AudioBackend&) = delete;
    AudioBackend& operator=(const AudioBackend&) = delete;
    AudioBackend(AudioBackend&&) = delete;
    AudioBackend& operator=(AudioBackend&&) = delete;
    virtual ~AudioBackend() = default;

    [[nodiscard]] virtual std::vector<AudioDeviceInfo> devices() = 0;
    // The callback runs on the audio thread from open until close returns.
    virtual StreamLayout open(const StreamRequest& request, AudioCallback callback) = 0;
    virtual void close() = 0;
};

StreamLayout concatenateDevices(const std::vector<AudioDeviceInfo>& available, const StreamRequest& request);

// Runs the callback only when a test or an offline render calls process, as fast as the machine allows.
class OfflineBackend final : public AudioBackend {
public:
    // Frame 0 sits at startTimeNs on the steady clock.
    explicit OfflineBackend(std::vector<AudioDeviceInfo> devices, std::int64_t startTimeNs = 0);
    ~OfflineBackend() override = default;
    OfflineBackend(const OfflineBackend&) = delete;
    OfflineBackend& operator=(const OfflineBackend&) = delete;
    OfflineBackend(OfflineBackend&&) = delete;
    OfflineBackend& operator=(OfflineBackend&&) = delete;

    [[nodiscard]] std::vector<AudioDeviceInfo> devices() override;
    StreamLayout open(const StreamRequest& request, AudioCallback callback) override;
    void close() override;

    void process(Frames frames);
    // Feeds samples to a device input channel from the current frame on; silence follows them.
    void setInput(std::string_view deviceId, int channel, std::vector<float> samples);
    [[nodiscard]] std::vector<float> output(std::string_view deviceId, int channel) const;
    [[nodiscard]] Frames processedFrames() const { return processed_; }
    [[nodiscard]] std::int64_t hostTimeAt(Frames frame) const;

private:
    struct Input {
        std::vector<float> samples;
        Frames startFrame = 0;
    };

    std::vector<AudioDeviceInfo> devices_;
    std::int64_t startTimeNs_ = 0;
    StreamLayout layout_;
    AudioCallback callback_;
    Frames processed_ = 0;
    std::map<int, Input> inputs_;
    std::vector<std::vector<float>> outputs_;
};

// Calls the callback at realtime speed from its own thread with silent fake devices, for headless runs.
class NullAudioBackend final : public AudioBackend {
public:
    explicit NullAudioBackend(std::vector<AudioDeviceInfo> devices = {{"null", "Null Device", 2, 2}});
    ~NullAudioBackend() override;
    NullAudioBackend(const NullAudioBackend&) = delete;
    NullAudioBackend& operator=(const NullAudioBackend&) = delete;
    NullAudioBackend(NullAudioBackend&&) = delete;
    NullAudioBackend& operator=(NullAudioBackend&&) = delete;

    [[nodiscard]] std::vector<AudioDeviceInfo> devices() override;
    StreamLayout open(const StreamRequest& request, AudioCallback callback) override;
    void close() override;

private:
    std::vector<AudioDeviceInfo> devices_;
    std::jthread thread_;
};

// Real devices through miniaudio. On macOS several devices open as one private aggregate device with drift
// compensation; elsewhere a stream opens one device.
class MiniaudioBackend final : public AudioBackend {
public:
    MiniaudioBackend();
    ~MiniaudioBackend() override;
    MiniaudioBackend(const MiniaudioBackend&) = delete;
    MiniaudioBackend& operator=(const MiniaudioBackend&) = delete;
    MiniaudioBackend(MiniaudioBackend&&) = delete;
    MiniaudioBackend& operator=(MiniaudioBackend&&) = delete;

    [[nodiscard]] std::vector<AudioDeviceInfo> devices() override;
    StreamLayout open(const StreamRequest& request, AudioCallback callback) override;
    void close() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace emerick::engine
