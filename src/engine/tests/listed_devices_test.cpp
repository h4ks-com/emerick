#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "private_aggregate.hpp"

using namespace emerick::engine;

TEST_CASE("our private aggregate coming and going is not a device change, a headset going away is") {
    const std::string aggregate = std::string{kAggregateUidPrefix} + "1";
    ListedDevices listed;
    listed.changed({"display", "headset:output"});

    CHECK_FALSE(listed.changed({"display", "headset:output", aggregate}));
    CHECK_FALSE(listed.changed({"headset:output", "display"}));
    CHECK(listed.changed({"display"}));
}
