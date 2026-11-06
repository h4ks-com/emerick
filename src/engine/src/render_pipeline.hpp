#pragma once

#include <algorithm>
#include <cfenv>
#include <cstddef>
#include <cstdint>
#include <semaphore>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

#include "emerick/engine/audio_backend.hpp"
#include "emerick/engine/plugin.hpp"

namespace emerick::engine {

struct Snapshot;

// The timeline frame at an offset into the block, which moves only while the transport runs, from where a count-in
// ended.
// A cycle jump at jumpOffset puts the timeline back at jumpFrame from there on.
struct BlockTime {
    Frames frame = 0;
    Frames rollFrom = 0;
    bool playing = false;
    Frames jumpOffset = 0;
    Frames jumpFrame = -1;

    [[nodiscard]] Frames at(Frames offset) const {
        if (!playing) {
            return frame;
        }
        return jumpFrame >= 0 && offset >= jumpOffset ? jumpFrame + offset - jumpOffset
                                                      : frame + std::max<Frames>(offset - rollFrom, 0);
    }
};

// Lets an offline render mix the buses of one block on a thread of their own while the tracks of the next block play,
// so a block takes as long as the slower half. Each block's output comes out one block late, behind a block of
// silence. Only an offline render uses it, since a block waits for the bus thread.
class RenderPipeline {
public:
    RenderPipeline();
    ~RenderPipeline();
    RenderPipeline(const RenderPipeline&) = delete;
    RenderPipeline& operator=(const RenderPipeline&) = delete;
    RenderPipeline(RenderPipeline&&) = delete;
    RenderPipeline& operator=(RenderPipeline&&) = delete;

    // While no block processes: sizes the buffers for a stream and queues a block of silence.
    void prepare(std::size_t outputs, Frames maxFrames);
    [[nodiscard]] Frames delay() const { return maxFrames_; }

    // The outputs the tracks of the next block play into.
    std::span<float* const> trackOutputs();
    // Queues the silent output of a block with nothing to play.
    void queueSilence(Frames frames);
    // Waits for the buses of the last block and queues their output.
    void finish();
    // Waits for the buses of the last block and drops their output, so their snapshot can go.
    void wait();
    // Hands the bus thread the block the tracks just played, with the sources each bus collected for it.
    void start(Snapshot& snapshot, const AudioBlock& block, const BlockTime& time,
               const InstrumentTransport& transport);
    // Takes the oldest frames of the queue into outputs.
    void deliver(std::span<float* const> outputs, Frames frames);

private:
    struct BusBlock {
        Snapshot* snapshot = nullptr;
        AudioBlock block;
        BlockTime time;
        InstrumentTransport transport;
    };

    void mixBuses(const std::stop_token& stop);

    Frames maxFrames_ = 0;
    std::vector<std::vector<float>> trackBuffers_;
    std::vector<std::vector<float>> busBuffers_;
    std::vector<float*> trackPointers_;
    std::vector<float*> busPointers_;
    std::vector<std::vector<float>> queue_;
    BusBlock busBlock_;
    // The render thread's floating point mode, so the bus thread flushes denormals exactly as the render thread does.
    std::fenv_t environment_{};
    bool mixing_ = false;
    std::binary_semaphore started_{0};
    std::binary_semaphore finished_{0};
    // Declared last, so it joins before the members its thread reads go away.
    std::jthread busThread_;
};

}  // namespace emerick::engine
