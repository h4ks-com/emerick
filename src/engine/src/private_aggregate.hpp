#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace emerick::engine {

inline constexpr std::string_view kAggregateUidPrefix = "emerick.aggregate.";

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

    [[nodiscard]] const std::string& uid() const { return uid_; }

private:
    std::uint32_t deviceId_ = 0;
    std::string uid_;
};

}  // namespace emerick::engine
