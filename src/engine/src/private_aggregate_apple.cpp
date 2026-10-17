#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "private_aggregate.hpp"

namespace emerick::engine {

namespace {

struct CfReleaser {
    void operator()(CFTypeRef object) const { CFRelease(object); }
};
template <class T>
using CfRef = std::unique_ptr<std::remove_pointer_t<T>, CfReleaser>;

CfRef<CFStringRef> cfString(const std::string& text) {
    return CfRef<CFStringRef>(CFStringCreateWithCString(nullptr, text.c_str(), kCFStringEncodingUTF8));
}

CfRef<CFNumberRef> cfNumber(int value) { return CfRef<CFNumberRef>(CFNumberCreate(nullptr, kCFNumberIntType, &value)); }

CfRef<CFMutableDictionaryRef> cfDictionary() {
    return CfRef<CFMutableDictionaryRef>(
        CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
}

CfRef<CFMutableDictionaryRef> subDevice(const std::string& uid, bool driftCompensated) {
    auto description = cfDictionary();
    CFDictionarySetValue(description.get(), CFSTR(kAudioSubDeviceUIDKey), cfString(uid).get());
    CFDictionarySetValue(description.get(), CFSTR(kAudioSubDeviceDriftCompensationKey),
                         cfNumber(driftCompensated ? 1 : 0).get());
    CFDictionarySetValue(description.get(), CFSTR(kAudioSubDeviceDriftCompensationQualityKey),
                         cfNumber(kAudioAggregateDriftCompensationMaxQuality).get());
    return description;
}

std::vector<AudioObjectID> deviceIds() {
    const AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                             kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size);
    std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, ids.data());
    return ids;
}

std::string uniqueUid() {
    const auto uuid = CfRef<CFUUIDRef>(CFUUIDCreate(nullptr));
    const auto text = CfRef<CFStringRef>(CFUUIDCreateString(nullptr, uuid.get()));
    std::array<char, 64> chars{};
    CFStringGetCString(text.get(), chars.data(), static_cast<CFIndex>(chars.size()), kCFStringEncodingUTF8);
    return std::string{kAggregateUidPrefix} + chars.data();
}

void waitUntilListed(AudioObjectID device) {
    using namespace std::chrono_literals;
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (!std::ranges::contains(deviceIds(), device) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }
}

}  // namespace

PrivateAggregate::PrivateAggregate(const std::vector<std::string>& subDeviceUids, double sampleRate)
    : uid_(uniqueUid()) {
    auto subDevices = CfRef<CFMutableArrayRef>(CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks));
    for (std::size_t index = 0; index < subDeviceUids.size(); ++index) {
        CFArrayAppendValue(subDevices.get(), subDevice(subDeviceUids[index], index > 0).get());
    }
    auto description = cfDictionary();
    CFDictionarySetValue(description.get(), CFSTR(kAudioAggregateDeviceNameKey), cfString("Emerick").get());
    CFDictionarySetValue(description.get(), CFSTR(kAudioAggregateDeviceUIDKey), cfString(uid_).get());
    CFDictionarySetValue(description.get(), CFSTR(kAudioAggregateDeviceIsPrivateKey), cfNumber(1).get());
    CFDictionarySetValue(description.get(), CFSTR(kAudioAggregateDeviceSubDeviceListKey), subDevices.get());
    CFDictionarySetValue(description.get(), CFSTR(kAudioAggregateDeviceMainSubDeviceKey),
                         cfString(subDeviceUids.front()).get());

    AudioObjectID device = kAudioObjectUnknown;
    if (AudioHardwareCreateAggregateDevice(description.get(), &device) != noErr) {
        throw std::runtime_error("cannot create a private aggregate audio device");
    }
    deviceId_ = device;
    waitUntilListed(device);

    const AudioObjectPropertyAddress rate{kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                          kAudioObjectPropertyElementMain};
    const Float64 nominal = sampleRate;
    if (AudioObjectSetPropertyData(device, &rate, 0, nullptr, sizeof(nominal), &nominal) != noErr) {
        AudioHardwareDestroyAggregateDevice(device);
        throw std::runtime_error("the aggregate audio device cannot run at " + std::to_string(sampleRate) + " Hz");
    }
}

PrivateAggregate::~PrivateAggregate() { AudioHardwareDestroyAggregateDevice(deviceId_); }

}  // namespace emerick::engine
