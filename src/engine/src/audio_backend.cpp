// The null backend's thread is the audio thread: it calls the callback once per block period at realtime speed.

#include "emerick/engine/audio_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <utility>

#include "clock.hpp"

namespace emerick::engine {

namespace {

const DeviceChannels* findDevice(const StreamLayout& layout, std::string_view deviceId) {
    const auto found = std::ranges::find(layout.devices, deviceId, &DeviceChannels::id);
    return found == layout.devices.end() ? nullptr : &*found;
}

std::string rateText(double rate) { return std::to_string(std::lround(rate)); }

}  // namespace

std::optional<std::string> unsupportedRate(const AudioDeviceInfo& device, double sampleRate) {
    if (device.sampleRates.empty() || std::ranges::contains(device.sampleRates, sampleRate)) {
        return std::nullopt;
    }
    if (device.sampleRates.size() == 1) {
        const std::string rate = rateText(device.sampleRates.front());
        return device.name + " runs at " + rate + " Hz; set Audio > Sample rate to " + rate + " and restart Emerick";
    }
    std::string rates;
    for (const double rate : device.sampleRates) {
        rates += (rates.empty() ? "" : ", ") + rateText(rate);
    }
    return device.name + " runs at " + rates + " Hz; set Audio > Sample rate to one of them and restart Emerick";
}

std::optional<int> StreamLayout::inputIndex(std::string_view deviceId, int channel) const {
    const DeviceChannels* device = findDevice(*this, deviceId);
    if (device == nullptr || channel < 1 || channel > device->inputs) {
        return std::nullopt;
    }
    return device->firstInput + channel - 1;
}

std::optional<int> StreamLayout::outputIndex(std::string_view deviceId, int channel) const {
    const DeviceChannels* device = findDevice(*this, deviceId);
    if (device == nullptr || channel < 1 || channel > device->outputs) {
        return std::nullopt;
    }
    return device->firstOutput + channel - 1;
}

StreamLayout concatenateDevices(const std::vector<AudioDeviceInfo>& available, const StreamRequest& request) {
    StreamLayout layout;
    layout.sampleRate = request.sampleRate;
    layout.maxFrames = request.blockFrames;
    std::vector<std::string> ids = request.deviceIds;
    if (ids.empty() && !available.empty()) {
        ids.push_back(available.front().id);
    }
    for (const std::string& id : ids) {
        const auto device = std::ranges::find(available, id, &AudioDeviceInfo::id);
        if (device == available.end()) {
            throw std::invalid_argument("unknown audio device " + id);
        }
        layout.devices.push_back(
            {id, layout.inputChannels, device->inputChannels, layout.outputChannels, device->outputChannels});
        layout.inputChannels += device->inputChannels;
        layout.outputChannels += device->outputChannels;
    }
    return layout;
}

OfflineBackend::OfflineBackend(std::vector<AudioDeviceInfo> devices, std::int64_t startTimeNs)
    : devices_(std::move(devices)), startTimeNs_(startTimeNs) {}

std::vector<AudioDeviceInfo> OfflineBackend::devices() { return devices_; }

StreamLayout OfflineBackend::open(const StreamRequest& request, AudioCallback callback) {
    layout_ = concatenateDevices(devices_, request);
    callback_ = std::move(callback);
    inputs_.clear();
    outputs_.assign(static_cast<std::size_t>(layout_.outputChannels), {});
    return layout_;
}

void OfflineBackend::close() { callback_ = nullptr; }

void OfflineBackend::process(Frames frames) {
    const auto inputCount = static_cast<std::size_t>(layout_.inputChannels);
    const auto outputCount = static_cast<std::size_t>(layout_.outputChannels);
    std::vector<std::vector<float>> inputBuffers(inputCount);
    std::vector<std::vector<float>> outputBuffers(outputCount);
    std::vector<const float*> inputPointers(inputCount);
    std::vector<float*> outputPointers(outputCount);
    while (frames > 0 && callback_) {
        const Frames block = std::min(frames, layout_.maxFrames);
        for (std::size_t channel = 0; channel < inputCount; ++channel) {
            std::vector<float>& buffer = inputBuffers[channel];
            buffer.assign(static_cast<std::size_t>(block), 0.0F);
            if (const auto input = inputs_.find(static_cast<int>(channel)); input != inputs_.end()) {
                const Input& source = input->second;
                for (Frames frame = 0; frame < block; ++frame) {
                    const Frames at = processed_ + frame - source.startFrame;
                    if (at >= 0 && std::cmp_less(at, source.samples.size())) {
                        buffer[static_cast<std::size_t>(frame)] = source.samples[static_cast<std::size_t>(at)];
                    }
                }
            }
            inputPointers[channel] = buffer.data();
        }
        for (std::size_t channel = 0; channel < outputCount; ++channel) {
            outputBuffers[channel].assign(static_cast<std::size_t>(block), 0.0F);
            outputPointers[channel] = outputBuffers[channel].data();
        }
        callback_({block, hostTimeAt(processed_), inputPointers, outputPointers});
        for (std::size_t channel = 0; channel < outputCount; ++channel) {
            outputs_[channel].insert(outputs_[channel].end(), outputBuffers[channel].begin(),
                                     outputBuffers[channel].end());
        }
        processed_ += block;
        frames -= block;
    }
}

void OfflineBackend::setInput(std::string_view deviceId, int channel, std::vector<float> samples) {
    const auto index = layout_.inputIndex(deviceId, channel);
    if (!index) {
        throw std::invalid_argument("no open input channel " + std::to_string(channel) + " on " +
                                    std::string(deviceId));
    }
    inputs_[*index] = {std::move(samples), processed_};
}

std::vector<float> OfflineBackend::output(std::string_view deviceId, int channel) const {
    const auto index = layout_.outputIndex(deviceId, channel);
    if (!index) {
        throw std::invalid_argument("no open output channel " + std::to_string(channel) + " on " +
                                    std::string(deviceId));
    }
    return outputs_[static_cast<std::size_t>(*index)];
}

std::int64_t OfflineBackend::hostTimeAt(Frames frame) const {
    return startTimeNs_ + framesToNs(frame, layout_.sampleRate);
}

NullAudioBackend::NullAudioBackend(std::vector<AudioDeviceInfo> devices) : devices_(std::move(devices)) {}

NullAudioBackend::~NullAudioBackend() { close(); }

std::vector<AudioDeviceInfo> NullAudioBackend::devices() { return devices_; }

StreamLayout NullAudioBackend::open(const StreamRequest& request, AudioCallback callback) {
    close();
    const StreamLayout layout = concatenateDevices(devices_, request);
    const auto frames = static_cast<std::size_t>(layout.maxFrames);
    std::vector<float> silence(frames, 0.0F);
    std::vector<std::vector<float>> outputBuffers(static_cast<std::size_t>(layout.outputChannels),
                                                  std::vector<float>(frames));
    std::vector<const float*> inputs(static_cast<std::size_t>(layout.inputChannels), silence.data());
    std::vector<float*> outputs;
    outputs.reserve(outputBuffers.size());
    for (std::vector<float>& buffer : outputBuffers) {
        outputs.push_back(buffer.data());
    }
    // NOLINTNEXTLINE(bugprone-exception-escape): the audio callback never throws, so a throw here should terminate.
    thread_ = std::jthread([layout, callback = std::move(callback), silence = std::move(silence),
                            outputBuffers = std::move(outputBuffers), inputs = std::move(inputs),
                            outputs = std::move(outputs)](const std::stop_token& stop) {
        const auto period = std::chrono::nanoseconds(framesToNs(layout.maxFrames, layout.sampleRate));
        auto deadline = std::chrono::steady_clock::now();
        while (!stop.stop_requested()) {
            callback({layout.maxFrames, steadyNowNs(), inputs, outputs});
            deadline += period;
            std::this_thread::sleep_until(deadline);
        }
    });
    return layout;
}

void NullAudioBackend::close() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

}  // namespace emerick::engine
