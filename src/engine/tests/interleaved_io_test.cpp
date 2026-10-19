#include "interleaved_io.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace emerick::engine;

TEST_CASE("each stream's channels keep their own index and long callbacks split into blocks") {
    constexpr Frames kFrames = 5;
    // Two devices as one aggregate: a stereo device, then a mono one.
    std::vector<float> stereoIn(kFrames * 2);
    std::vector<float> monoIn(kFrames);
    for (Frames frame = 0; frame < kFrames; ++frame) {
        const auto at = static_cast<std::size_t>(frame);
        stereoIn[(at * 2)] = 10.0F + static_cast<float>(frame);
        stereoIn[(at * 2) + 1] = 20.0F + static_cast<float>(frame);
        monoIn[at] = 30.0F + static_cast<float>(frame);
    }
    std::vector<float> stereoOut(kFrames * 2);
    std::vector<float> monoOut(kFrames);
    const std::vector<InterleavedInput> inputs{{stereoIn.data(), 2}, {monoIn.data(), 1}};
    const std::vector<InterleavedOutput> outputs{{stereoOut.data(), 2}, {monoOut.data(), 1}};

    PlanarAdapter adapter(3, 3, 2);
    std::vector<Frames> blocks;
    std::vector<float> thirdChannel;
    const AudioCallback callback = [&](const AudioBlock& block) {
        blocks.push_back(block.frames);
        for (Frames frame = 0; frame < block.frames; ++frame) {
            thirdChannel.push_back(block.inputs[2][frame]);
            for (std::size_t channel = 0; channel < 3; ++channel) {
                block.outputs[channel][frame] = -block.inputs[channel][frame];
            }
        }
    };
    adapter.process(inputs, outputs, kFrames, 0, 48000.0, callback);

    CHECK(blocks == std::vector<Frames>{2, 2, 1});
    CHECK(thirdChannel == std::vector<float>{30, 31, 32, 33, 34});
    CHECK(stereoOut == std::vector<float>{-10, -20, -11, -21, -12, -22, -13, -23, -14, -24});
    CHECK(monoOut == std::vector<float>{-30, -31, -32, -33, -34});
}

TEST_CASE("a missing input stream reads as silence") {
    const std::vector<InterleavedInput> inputs{{nullptr, 2}};
    PlanarAdapter adapter(2, 0, 4);
    float heard = 1.0F;
    adapter.process(inputs, {}, 4, 0, 48000.0, [&](const AudioBlock& block) { heard = block.inputs[1][3]; });
    CHECK(heard == 0.0F);
}
