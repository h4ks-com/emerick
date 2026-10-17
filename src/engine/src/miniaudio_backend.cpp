// miniaudio's device thread is the audio thread: it calls the callback once per device period.

#include <miniaudio.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>

#include "clock.hpp"
#include "emerick/engine/audio_backend.hpp"

#ifdef __APPLE__
#include "device_list_listener.hpp"
#include "private_aggregate.hpp"
#endif

namespace emerick::engine {

namespace {

std::string fromChars(std::span<const char> chars) { return {chars.data(), strnlen(chars.data(), chars.size())}; }

void toChars(const std::string& text, std::span<char> chars) { text.copy(chars.data(), chars.size() - 1); }

std::string idText(const ma_context& context, const ma_device_info& info) {
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access): ma_device_id is a union keyed by the context backend.
    switch (context.backend) {
        case ma_backend_coreaudio:
            return fromChars(info.id.coreaudio);
        case ma_backend_alsa:
            return fromChars(info.id.alsa);
        case ma_backend_pulseaudio:
            return fromChars(info.id.pulse);
        default:
            return fromChars(info.name);
    }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
}

ma_device_id idFromText(const ma_context& context, const std::string& text) {
    ma_device_id id{};
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access): ma_device_id is a union keyed by the context backend.
    switch (context.backend) {
        case ma_backend_coreaudio:
            toChars(text, id.coreaudio);
            break;
        case ma_backend_alsa:
            toChars(text, id.alsa);
            break;
        case ma_backend_pulseaudio:
            toChars(text, id.pulse);
            break;
        default:
            throw std::invalid_argument("this audio system cannot open a device by id");
    }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    return id;
}

int maxChannels(const ma_device_info& info) {
    ma_uint32 channels = 0;
    for (ma_uint32 index = 0; index < info.nativeDataFormatCount; ++index) {
        channels = std::max(channels, info.nativeDataFormats[index].channels);
    }
    return static_cast<int>(channels);
}

// CoreAudio lists the nominal rates the hardware runs at; the other systems resample, so any rate works there.
void addSampleRates(const ma_context& context, const ma_device_info& info, std::vector<double>& rates) {
    if (context.backend != ma_backend_coreaudio) {
        return;
    }
    for (ma_uint32 index = 0; index < info.nativeDataFormatCount; ++index) {
        const auto rate = static_cast<double>(info.nativeDataFormats[index].sampleRate);
        if (rate > 0 && !std::ranges::contains(rates, rate)) {
            rates.push_back(rate);
        }
    }
    std::ranges::sort(rates);
}

}  // namespace

struct MiniaudioBackend::Impl {
    ma_context context{};
    std::optional<ma_device> device;
    AudioCallback callback;
    StreamLayout layout;
    std::vector<std::vector<float>> inputs;
    std::vector<std::vector<float>> outputs;
    std::vector<const float*> inputPointers;
    std::vector<float*> outputPointers;
#ifdef __APPLE__
    std::optional<PrivateAggregate> aggregate;
    std::optional<DeviceListListener> listener;
#endif

    static void onData(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
        static_cast<Impl*>(device->pUserData)
            ->process(static_cast<float*>(output), static_cast<const float*>(input), frameCount);
    }

    void process(float* output, const float* input, ma_uint32 frameCount) {
        const auto inputCount = inputs.size();
        const auto outputCount = outputs.size();
        auto remaining = static_cast<Frames>(frameCount);
        std::int64_t hostTime = steadyNowNs();
        while (remaining > 0) {
            const Frames frames = std::min(remaining, layout.maxFrames);
            for (std::size_t channel = 0; channel < inputCount; ++channel) {
                for (Frames frame = 0; frame < frames; ++frame) {
                    inputs[channel][static_cast<std::size_t>(frame)] =
                        input != nullptr ? input[static_cast<std::size_t>(frame) * inputCount + channel] : 0.0F;
                }
            }
            callback({frames, hostTime, inputPointers, outputPointers});
            for (std::size_t channel = 0; channel < outputCount; ++channel) {
                for (Frames frame = 0; frame < frames; ++frame) {
                    output[static_cast<std::size_t>(frame) * outputCount + channel] =
                        outputs[channel][static_cast<std::size_t>(frame)];
                }
            }
            remaining -= frames;
            if (input != nullptr) {
                input += static_cast<std::size_t>(frames) * inputCount;
            }
            output += static_cast<std::size_t>(frames) * outputCount;
            hostTime += framesToNs(frames, layout.sampleRate);
        }
    }
};

MiniaudioBackend::MiniaudioBackend() : impl_(std::make_unique<Impl>()) {
    if (ma_context_init(nullptr, 0, nullptr, &impl_->context) != MA_SUCCESS) {
        throw std::runtime_error("cannot start the audio system");
    }
#ifdef __APPLE__
    impl_->listener.emplace([this] { deviceListChanged(); });
#endif
}

MiniaudioBackend::~MiniaudioBackend() {
#ifdef __APPLE__
    impl_->listener.reset();
#endif
    close();
    ma_context_uninit(&impl_->context);
}

std::vector<AudioDeviceInfo> MiniaudioBackend::devices() {
    ma_device_info* playback = nullptr;
    ma_device_info* capture = nullptr;
    ma_uint32 playbackCount = 0;
    ma_uint32 captureCount = 0;
    if (ma_context_get_devices(&impl_->context, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS) {
        throw std::runtime_error("cannot list audio devices");
    }
    std::vector<AudioDeviceInfo> result;
    const auto add = [&](std::span<const ma_device_info> infos, ma_device_type type) {
        for (const ma_device_info& listed : infos) {
            ma_device_info info{};
            if (ma_context_get_device_info(&impl_->context, type, &listed.id, &info) != MA_SUCCESS) {
                continue;
            }
            const std::string id = idText(impl_->context, listed);
#ifdef __APPLE__
            if (id.starts_with(kAggregateUidPrefix)) {
                continue;
            }
#endif
            auto found = std::ranges::find(result, id, &AudioDeviceInfo::id);
            if (found == result.end()) {
                result.push_back({id, fromChars(listed.name)});
                found = std::prev(result.end());
            }
            (type == ma_device_type_playback ? found->outputChannels : found->inputChannels) = maxChannels(info);
            addSampleRates(impl_->context, info, found->sampleRates);
        }
    };
    add({playback, playbackCount}, ma_device_type_playback);
    add({capture, captureCount}, ma_device_type_capture);
    return result;
}

StreamLayout MiniaudioBackend::open(const StreamRequest& request, AudioCallback callback) {
    close();
    const std::vector<AudioDeviceInfo>& available = request.available;
    std::string deviceId;
    AudioDeviceInfo opened{"default", "Default output", 0, 2};
    if (request.deviceIds.size() == 1) {
        deviceId = request.deviceIds.front();
        const auto found = std::ranges::find(available, deviceId, &AudioDeviceInfo::id);
        if (found == available.end()) {
            throw std::invalid_argument("unknown audio device " + deviceId);
        }
        opened = *found;
    } else if (request.deviceIds.size() > 1) {
#ifdef __APPLE__
        const StreamLayout combined = concatenateDevices(available, request);
        impl_->aggregate.emplace(request.deviceIds, request.sampleRate);
        deviceId = impl_->aggregate->uid();
        opened = {deviceId, "Emerick aggregate", combined.inputChannels, combined.outputChannels};
#else
        throw std::invalid_argument(
            "several audio devices in one session need a private aggregate device, "
            "which only macOS provides; use PipeWire or JACK to combine devices");
#endif
    }

    ma_device_type type = ma_device_type_playback;
    if (opened.inputChannels > 0) {
        type = opened.outputChannels > 0 ? ma_device_type_duplex : ma_device_type_capture;
    }
    ma_device_config config = ma_device_config_init(type);
    const std::optional<ma_device_id> id =
        deviceId.empty() ? std::nullopt : std::optional(idFromText(impl_->context, deviceId));
    config.playback.pDeviceID = id ? &*id : nullptr;
    config.capture.pDeviceID = id ? &*id : nullptr;
    config.playback.format = ma_format_f32;
    config.capture.format = ma_format_f32;
    config.playback.channels = static_cast<ma_uint32>(opened.outputChannels);
    config.capture.channels = static_cast<ma_uint32>(opened.inputChannels);
    config.sampleRate = static_cast<ma_uint32>(request.sampleRate);
    config.periodSizeInFrames = static_cast<ma_uint32>(request.blockFrames);
    config.coreaudio.allowNominalSampleRateChange = MA_TRUE;
    config.dataCallback = &Impl::onData;
    config.pUserData = impl_.get();

    impl_->device.emplace();
    if (ma_device_init(&impl_->context, &config, &*impl_->device) != MA_SUCCESS) {
        impl_->device.reset();
        close();
        throw std::runtime_error("cannot open audio device " + opened.name);
    }

    StreamLayout& layout = impl_->layout;
    layout = concatenateDevices({opened}, {{opened.id}, request.sampleRate, request.blockFrames});
    if (request.deviceIds.size() > 1) {
        layout.devices = concatenateDevices(available, request).devices;
    }
    layout.sampleRate = impl_->device->sampleRate;
    impl_->inputs.assign(static_cast<std::size_t>(layout.inputChannels),
                         std::vector<float>(static_cast<std::size_t>(layout.maxFrames)));
    impl_->outputs.assign(static_cast<std::size_t>(layout.outputChannels),
                          std::vector<float>(static_cast<std::size_t>(layout.maxFrames)));
    impl_->inputPointers.clear();
    impl_->outputPointers.clear();
    for (const std::vector<float>& buffer : impl_->inputs) {
        impl_->inputPointers.push_back(buffer.data());
    }
    for (std::vector<float>& buffer : impl_->outputs) {
        impl_->outputPointers.push_back(buffer.data());
    }
    impl_->callback = std::move(callback);
    if (ma_device_start(&*impl_->device) != MA_SUCCESS) {
        close();
        throw std::runtime_error("cannot start audio device " + opened.name);
    }
    return layout;
}

void MiniaudioBackend::close() {
    if (impl_->device) {
        ma_device_uninit(&*impl_->device);
        impl_->device.reset();
    }
#ifdef __APPLE__
    impl_->aggregate.reset();
#endif
}

}  // namespace emerick::engine
