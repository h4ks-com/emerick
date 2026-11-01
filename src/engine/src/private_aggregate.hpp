#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace emerick::engine {

inline constexpr std::string_view kAggregateUidPrefix = "emerick.aggregate.";

// We create and destroy a private aggregate with every stream that spans devices, so it never counts as a device
// change, or each open would list the devices again and could change the routes that opened it.
class ListedDevices {
public:
    // Returns whether the uids, leaving out our aggregates, differ from those of the last call.
    bool changed(std::vector<std::string> uids) {
        std::erase_if(uids, [](const std::string& uid) { return uid.starts_with(kAggregateUidPrefix); });
        std::ranges::sort(uids);
        const std::scoped_lock lock(mutex_);
        const bool differs = uids != uids_;
        uids_ = std::move(uids);
        return differs;
    }

private:
    std::mutex mutex_;
    std::vector<std::string> uids_;
};

// A CoreAudio aggregate device only this process sees. The first sub-device is the clock and the others are
// drift compensated; its channels are the sub-devices' channels in order.
class PrivateAggregate {
public:
    PrivateAggregate(const std::vector<std::string>& subDeviceUids, double sampleRate);
    ~PrivateAggregate();
    PrivateAggregate(const PrivateAggregate&) = delete;
    PrivateAggregate& operator=(const PrivateAggregate&) = delete;
    PrivateAggregate(PrivateAggregate&&) = delete;
    PrivateAggregate& operator=(PrivateAggregate&&) = delete;

    [[nodiscard]] std::uint32_t deviceId() const { return deviceId_; }

private:
    std::uint32_t deviceId_ = 0;
};

}  // namespace emerick::engine
