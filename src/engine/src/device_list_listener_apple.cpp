#include <CoreAudio/CoreAudio.h>

#include <utility>

#include "device_list_listener.hpp"

namespace emerick::engine {

namespace {

constexpr AudioObjectPropertyAddress kDevices{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                              kAudioObjectPropertyElementMain};

OSStatus devicesChanged(AudioObjectID /*object*/, UInt32 /*count*/, const AudioObjectPropertyAddress* /*addresses*/,
                        void* listener) {
    (*static_cast<std::function<void()>*>(listener))();
    return noErr;
}

}  // namespace

DeviceListListener::DeviceListListener(std::function<void()> onChange) : onChange_(std::move(onChange)) {
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &kDevices, &devicesChanged, &onChange_);
}

DeviceListListener::~DeviceListListener() {
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &kDevices, &devicesChanged, &onChange_);
}

}  // namespace emerick::engine
