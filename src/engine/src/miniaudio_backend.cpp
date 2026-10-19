// miniaudio's device thread is the audio thread: it calls the callback once per device period.

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <stdexcept>

#include "clock.hpp"
#include "emerick/engine/audio_backend.hpp"
#include "interleaved_io.hpp"

namespace emerick::engine {

namespace {

std::string fromChars(std::span<const char> chars) { return {chars.data(), strnlen(chars.data(), chars.size())}; }

void toChars(const std::string& text, std::span<char> chars) { text.copy(chars.data(), chars.size() - 1); }

std::string idText(const ma_context& context, const ma_device_info& info) {
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access): ma_device_id is a union keyed by the context backend.
    switch (context.backend) {
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

}  // namespace

struct MiniaudioBackend::Impl {
    ma_context context{};
    std::optional<ma_device> device;
    AudioCallback callback;
    StreamLayout layout;
    PlanarAdapter adapter;

    static void onData(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
        static_cast<Impl*>(device->pUserData)
            ->process(static_cast<float*>(output), static_cast<const float*>(input), frameCount);
    }

    void process(float* output, const float* input, ma_uint32 frameCount) {
        flushDenormalsToZero();
        const std::array inputs{InterleavedInput{input, layout.inputChannels}};
        const std::array outputs{InterleavedOutput{output, layout.outputChannels}};
        adapter.process(inputs, outputs, static_cast<Frames>(frameCount), steadyNowNs(), layout.sampleRate, callback);
    }
};

MiniaudioBackend::MiniaudioBackend() : impl_(std::make_unique<Impl>()) {
    if (ma_context_init(nullptr, 0, nullptr, &impl_->context) != MA_SUCCESS) {
        throw std::runtime_error("cannot start the audio system");
    }
}

MiniaudioBackend::~MiniaudioBackend() {
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
            auto found = std::ranges::find(result, id, &AudioDeviceInfo::id);
            if (found == result.end()) {
                result.push_back({id, fromChars(listed.name)});
                found = std::prev(result.end());
            }
            (type == ma_device_type_playback ? found->outputChannels : found->inputChannels) = maxChannels(info);
        }
    };
    add({playback, playbackCount}, ma_device_type_playback);
    add({capture, captureCount}, ma_device_type_capture);
    return result;
}

StreamLayout MiniaudioBackend::open(const StreamRequest& request, AudioCallback callback) {
    close();
    if (request.deviceIds.size() > 1) {
        throw std::invalid_argument(
            "several audio devices in one session need a private aggregate device, "
            "which only macOS provides; use PipeWire or JACK to combine devices");
    }
    std::string deviceId;
    AudioDeviceInfo opened{"default", "Default output", 0, 2};
    if (request.deviceIds.size() == 1) {
        deviceId = request.deviceIds.front();
        const auto found = std::ranges::find(request.available, deviceId, &AudioDeviceInfo::id);
        if (found == request.available.end()) {
            throw std::invalid_argument("unknown audio device " + deviceId);
        }
        opened = *found;
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
    // Without a map miniaudio assumes speaker positions and remixes our channels into the device's; a blank map keeps
    // every channel at its own index.
    std::array<ma_channel, MA_MAX_CHANNELS> blankMap{};
    config.playback.pChannelMap = blankMap.data();
    config.capture.pChannelMap = blankMap.data();
    config.sampleRate = static_cast<ma_uint32>(request.sampleRate);
    config.periodSizeInFrames = static_cast<ma_uint32>(request.blockFrames);
    config.dataCallback = &Impl::onData;
    config.pUserData = impl_.get();

    impl_->device.emplace();
    if (ma_device_init(&impl_->context, &config, &*impl_->device) != MA_SUCCESS) {
        impl_->device.reset();
        throw std::runtime_error("cannot open audio device " + opened.name);
    }

    StreamLayout& layout = impl_->layout;
    layout = concatenateDevices({opened}, {{opened.id}, request.sampleRate, request.blockFrames});
    layout.sampleRate = impl_->device->sampleRate;
    impl_->adapter = PlanarAdapter(layout.inputChannels, layout.outputChannels, layout.maxFrames);
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
}

}  // namespace emerick::engine
