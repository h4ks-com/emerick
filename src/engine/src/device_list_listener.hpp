#pragma once

#include <functional>

namespace emerick::engine {

// Calls onChange on a CoreAudio thread each time a device appears or goes away, until it is destroyed.
class DeviceListListener {
public:
    explicit DeviceListListener(std::function<void()> onChange);
    ~DeviceListListener();
    DeviceListListener(const DeviceListListener&) = delete;
    DeviceListListener& operator=(const DeviceListListener&) = delete;
    DeviceListListener(DeviceListListener&&) = delete;
    DeviceListListener& operator=(DeviceListListener&&) = delete;

private:
    std::function<void()> onChange_;
};

}  // namespace emerick::engine
