// The CoreAudio HAL's IO thread is the audio thread: it calls ioProc once per device buffer with every input and output
// stream of the device.

#include <CoreAudio/CoreAudio.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <thread>

#include "clock.hpp"
#include "device_list_listener.hpp"
#include "emerick/engine/audio_backend.hpp"
#include "interleaved_io.hpp"
#include "private_aggregate.hpp"

namespace emerick::engine {

namespace {

constexpr std::array kCommonRates{44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};

AudioObjectPropertyAddress address(AudioObjectPropertySelector selector,
                                   AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    return {selector, scope, kAudioObjectPropertyElementMain};
}

template <class T>
std::optional<T> property(AudioObjectID object, const AudioObjectPropertyAddress& where) {
    T value{};
    UInt32 size = sizeof(T);
    if (AudioObjectGetPropertyData(object, &where, 0, nullptr, &size, static_cast<void*>(&value)) != noErr) {
        return std::nullopt;
    }
    return value;
}

template <class T>
std::vector<T> arrayProperty(AudioObjectID object, const AudioObjectPropertyAddress& where) {
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(object, &where, 0, nullptr, &size) != noErr) {
        return {};
    }
    std::vector<T> values(size / sizeof(T));
    if (AudioObjectGetPropertyData(object, &where, 0, nullptr, &size, values.data()) != noErr) {
        return {};
    }
    values.resize(size / sizeof(T));
    return values;
}

template <class T>
bool setProperty(AudioObjectID object, const AudioObjectPropertyAddress& where, T value) {
    return AudioObjectSetPropertyData(object, &where, 0, nullptr, sizeof(T), &value) == noErr;
}

std::string text(CFStringRef string) {
    if (string == nullptr) {
        return {};
    }
    std::array<char, 512> chars{};
    CFStringGetCString(string, chars.data(), static_cast<CFIndex>(chars.size()), kCFStringEncodingUTF8);
    CFRelease(string);
    return chars.data();
}

std::string stringProperty(AudioObjectID object, AudioObjectPropertySelector selector) {
    return text(property<CFStringRef>(object, address(selector)).value_or(nullptr));
}

// AudioBufferList ends in a one-element array that really holds mNumberBuffers buffers.
template <class List>
auto buffers(List& list) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    return std::span(list.mBuffers, list.mNumberBuffers);
}

Frames framesIn(const AudioBuffer& buffer) {
    return buffer.mNumberChannels == 0
               ? 0
               : static_cast<Frames>(buffer.mDataByteSize / (sizeof(float) * buffer.mNumberChannels));
}

std::vector<int> streamChannels(AudioObjectID device, AudioObjectPropertyScope scope) {
    const AudioObjectPropertyAddress where = address(kAudioDevicePropertyStreamConfiguration, scope);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &where, 0, nullptr, &size) != noErr || size == 0) {
        return {};
    }
    std::vector<AudioBufferList> storage((size + sizeof(AudioBufferList) - 1) / sizeof(AudioBufferList));
    if (AudioObjectGetPropertyData(device, &where, 0, nullptr, &size, storage.data()) != noErr) {
        return {};
    }
    return buffers(storage.front()) |
           std::views::transform([](const AudioBuffer& buffer) { return static_cast<int>(buffer.mNumberChannels); }) |
           std::ranges::to<std::vector>();
}

std::vector<double> nominalRates(AudioObjectID device) {
    std::vector<double> rates;
    for (const AudioValueRange& range :
         arrayProperty<AudioValueRange>(device, address(kAudioDevicePropertyAvailableNominalSampleRates))) {
        if (range.mMinimum == range.mMaximum) {
            rates.push_back(range.mMinimum);
            continue;
        }
        for (const double rate : kCommonRates) {
            if (rate >= range.mMinimum && rate <= range.mMaximum) {
                rates.push_back(rate);
            }
        }
    }
    std::ranges::sort(rates);
    const auto [first, last] = std::ranges::unique(rates);
    rates.erase(first, last);
    return rates;
}

AudioObjectID deviceForUid(const std::string& uid) {
    CFStringRef name = CFStringCreateWithCString(nullptr, uid.c_str(), kCFStringEncodingUTF8);
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    const AudioObjectPropertyAddress where = address(kAudioHardwarePropertyTranslateUIDToDevice);
    const OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &where, sizeof(CFStringRef),
                                                       static_cast<const void*>(&name), &size, &device);
    CFRelease(name);
    if (status != noErr || device == kAudioObjectUnknown) {
        throw std::invalid_argument("unknown audio device " + uid);
    }
    return device;
}

// Changing the nominal rate finishes on a CoreAudio thread, so we wait until the device reports it.
double runAt(AudioObjectID device, double sampleRate) {
    using namespace std::chrono_literals;
    const AudioObjectPropertyAddress rate = address(kAudioDevicePropertyNominalSampleRate);
    if (property<Float64>(device, rate) != sampleRate) {
        setProperty<Float64>(device, rate, sampleRate);
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (property<Float64>(device, rate) != sampleRate && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(10ms);
        }
    }
    return property<Float64>(device, rate).value_or(0.0);
}

Frames useBufferSize(AudioObjectID device, Frames frames) {
    const auto range = property<AudioValueRange>(device, address(kAudioDevicePropertyBufferFrameSizeRange));
    const double wanted =
        range ? std::clamp(static_cast<double>(frames), range->mMinimum, range->mMaximum) : static_cast<double>(frames);
    const AudioObjectPropertyAddress size = address(kAudioDevicePropertyBufferFrameSize);
    setProperty<UInt32>(device, size, static_cast<UInt32>(wanted));
    return property<UInt32>(device, size).value_or(0);
}

Frames latency(AudioObjectID device, AudioObjectPropertyScope scope) {
    const UInt32 deviceFrames = property<UInt32>(device, address(kAudioDevicePropertyLatency, scope)).value_or(0) +
                                property<UInt32>(device, address(kAudioDevicePropertySafetyOffset, scope)).value_or(0);
    const std::vector<AudioStreamID> streams =
        arrayProperty<AudioStreamID>(device, address(kAudioDevicePropertyStreams, scope));
    const UInt32 streamFrames =
        streams.empty() ? 0 : property<UInt32>(streams.front(), address(kAudioStreamPropertyLatency)).value_or(0);
    return deviceFrames + streamFrames;
}

std::vector<AudioObjectID> allDevices() {
    return arrayProperty<AudioObjectID>(kAudioObjectSystemObject, address(kAudioHardwarePropertyDevices));
}

int sum(const std::vector<int>& values) { return std::ranges::fold_left(values, 0, std::plus{}); }

}  // namespace

struct CoreAudioBackend::Impl {
    std::optional<DeviceListListener> listener;
    std::optional<PrivateAggregate> aggregate;
    AudioObjectID device = kAudioObjectUnknown;
    AudioDeviceIOProcID procId = nullptr;
    AudioCallback callback;
    StreamLayout layout;
    PlanarAdapter adapter;
    std::vector<InterleavedInput> inputs;
    std::vector<InterleavedOutput> outputs;

    static OSStatus ioProc(AudioObjectID /*device*/, const AudioTimeStamp* /*now*/, const AudioBufferList* input,
                           const AudioTimeStamp* /*inputTime*/, AudioBufferList* output,
                           const AudioTimeStamp* /*outputTime*/, void* impl) {
        static_cast<Impl*>(impl)->process(input, output);
        return noErr;
    }

    void process(const AudioBufferList* input, AudioBufferList* output) {
        flushDenormalsToZero();
        Frames frames = 0;
        const auto inputBuffers = input != nullptr ? buffers(*input) : std::span<const AudioBuffer>{};
        const auto outputBuffers = output != nullptr ? buffers(*output) : std::span<AudioBuffer>{};
        for (std::size_t index = 0; index < inputs.size(); ++index) {
            const bool present = index < inputBuffers.size();
            inputs[index].samples = present ? static_cast<const float*>(inputBuffers[index].mData) : nullptr;
            frames = std::max(frames, present ? framesIn(inputBuffers[index]) : 0);
        }
        for (std::size_t index = 0; index < outputs.size(); ++index) {
            const bool present = index < outputBuffers.size();
            outputs[index].samples = present ? static_cast<float*>(outputBuffers[index].mData) : nullptr;
            frames = std::max(frames, present ? framesIn(outputBuffers[index]) : 0);
        }
        adapter.process(inputs, outputs, frames, steadyNowNs(), layout.sampleRate, callback);
    }
};

CoreAudioBackend::CoreAudioBackend() : impl_(std::make_unique<Impl>()) {
    impl_->listener.emplace([this] { deviceListChanged(); });
}

CoreAudioBackend::~CoreAudioBackend() {
    impl_->listener.reset();
    close();
}

std::vector<AudioDeviceInfo> CoreAudioBackend::devices() {
    std::vector<AudioDeviceInfo> result;
    for (const AudioObjectID device : allDevices()) {
        const std::string uid = stringProperty(device, kAudioDevicePropertyDeviceUID);
        const int inputs = sum(streamChannels(device, kAudioObjectPropertyScopeInput));
        const int outputs = sum(streamChannels(device, kAudioObjectPropertyScopeOutput));
        if (uid.empty() || uid.starts_with(kAggregateUidPrefix) || inputs + outputs == 0) {
            continue;
        }
        result.push_back(
            {uid, stringProperty(device, kAudioObjectPropertyName), inputs, outputs, nominalRates(device)});
    }
    return result;
}

StreamLayout CoreAudioBackend::open(const StreamRequest& request, AudioCallback callback) {
    close();
    Impl& impl = *impl_;
    StreamRequest opened = request;
    if (request.deviceIds.size() > 1) {
        impl.aggregate.emplace(request.deviceIds, request.sampleRate);
        impl.device = impl.aggregate->deviceId();
    } else if (request.deviceIds.size() == 1) {
        impl.device = deviceForUid(request.deviceIds.front());
    } else {
        impl.device =
            property<AudioObjectID>(kAudioObjectSystemObject, address(kAudioHardwarePropertyDefaultOutputDevice))
                .value_or(kAudioObjectUnknown);
        opened.deviceIds = {stringProperty(impl.device, kAudioDevicePropertyDeviceUID)};
    }
    StreamLayout layout = concatenateDevices(request.available, opened);
    const std::string name = request.deviceIds.size() > 1 ? "the combined audio devices"
                                                          : stringProperty(impl.device, kAudioObjectPropertyName);

    layout.sampleRate = runAt(impl.device, request.sampleRate);
    if (layout.sampleRate != request.sampleRate) {
        close();
        throw std::runtime_error(name + " does not run at " + std::to_string(std::lround(request.sampleRate)) +
                                 " Hz; set Audio > Sample rate to a rate it lists and restart Emerick");
    }
    layout.maxFrames = useBufferSize(impl.device, request.blockFrames);
    layout.inputLatency = layout.maxFrames + latency(impl.device, kAudioObjectPropertyScopeInput);
    layout.outputLatency = layout.maxFrames + latency(impl.device, kAudioObjectPropertyScopeOutput);

    const std::vector<int> inputStreams = streamChannels(impl.device, kAudioObjectPropertyScopeInput);
    const std::vector<int> outputStreams = streamChannels(impl.device, kAudioObjectPropertyScopeOutput);
    if (sum(inputStreams) != layout.inputChannels || sum(outputStreams) != layout.outputChannels ||
        layout.maxFrames <= 0) {
        close();
        throw std::runtime_error("cannot open " + name + " with all of its channels");
    }
    impl.inputs = inputStreams |
                  std::views::transform([](int channels) { return InterleavedInput{nullptr, channels}; }) |
                  std::ranges::to<std::vector>();
    impl.outputs = outputStreams |
                   std::views::transform([](int channels) { return InterleavedOutput{nullptr, channels}; }) |
                   std::ranges::to<std::vector>();
    impl.adapter = PlanarAdapter(layout.inputChannels, layout.outputChannels, layout.maxFrames);
    impl.layout = layout;
    impl.callback = std::move(callback);

    if (AudioDeviceCreateIOProcID(impl.device, &Impl::ioProc, impl_.get(), &impl.procId) != noErr ||
        AudioDeviceStart(impl.device, impl.procId) != noErr) {
        close();
        throw std::runtime_error("cannot start " + name);
    }
    return layout;
}

void CoreAudioBackend::close() {
    Impl& impl = *impl_;
    if (impl.procId != nullptr) {
        AudioDeviceStop(impl.device, impl.procId);
        AudioDeviceDestroyIOProcID(impl.device, impl.procId);
        impl.procId = nullptr;
    }
    impl.aggregate.reset();
    impl.device = kAudioObjectUnknown;
}

}  // namespace emerick::engine
