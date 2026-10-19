#pragma once

#include <span>
#include <vector>

#include "emerick/engine/audio_backend.hpp"

namespace emerick::engine {

// One device stream as the system hands it over: frames of interleaved channels.
struct InterleavedInput {
    const float* samples = nullptr;  // null reads as silence
    int channels = 0;
};

struct InterleavedOutput {
    float* samples = nullptr;  // null drops what the callback wrote
    int channels = 0;
};

// Turns a device callback's interleaved streams into the planar blocks AudioCallback takes. Stream channels follow
// each other in order, so the second stream's first channel has the index of the first stream's channel count.
class PlanarAdapter {
public:
    PlanarAdapter() = default;
    PlanarAdapter(int inputChannels, int outputChannels, Frames maxFrames);

    // Realtime: splits frames into blocks of at most maxFrames, so the callback never sees a longer block.
    void process(std::span<const InterleavedInput> inputs, std::span<const InterleavedOutput> outputs, Frames frames,
                 std::int64_t hostTimeNs, double sampleRate, const AudioCallback& callback);

private:
    Frames maxFrames_ = 0;
    std::vector<std::vector<float>> inputs_;
    std::vector<std::vector<float>> outputs_;
    std::vector<const float*> inputPointers_;
    std::vector<float*> outputPointers_;
};

// Realtime: makes this thread treat denormal floats as zero, which keeps decaying signals from slowing the CPU.
void flushDenormalsToZero();

}  // namespace emerick::engine
