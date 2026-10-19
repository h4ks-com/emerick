#include "interleaved_io.hpp"

#include <algorithm>
#include <cstdint>

#include "clock.hpp"

#ifdef __x86_64__
#include <xmmintrin.h>
#endif

namespace emerick::engine {

namespace {

void deinterleave(std::span<const InterleavedInput> streams, Frames offset, Frames frames,
                  std::vector<std::vector<float>>& planar) {
    std::size_t channel = 0;
    for (const InterleavedInput& stream : streams) {
        const auto width = static_cast<std::size_t>(stream.channels);
        for (std::size_t inStream = 0; inStream < width && channel < planar.size(); ++inStream, ++channel) {
            float* destination = planar[channel].data();
            if (stream.samples == nullptr) {
                std::fill_n(destination, frames, 0.0F);
                continue;
            }
            const float* source = stream.samples + (static_cast<std::size_t>(offset) * width) + inStream;
            for (Frames frame = 0; frame < frames; ++frame) {
                destination[frame] = source[static_cast<std::size_t>(frame) * width];
            }
        }
    }
}

void interleave(const std::vector<std::vector<float>>& planar, Frames offset, Frames frames,
                std::span<const InterleavedOutput> streams) {
    std::size_t channel = 0;
    for (const InterleavedOutput& stream : streams) {
        const auto width = static_cast<std::size_t>(stream.channels);
        for (std::size_t inStream = 0; inStream < width && channel < planar.size(); ++inStream, ++channel) {
            if (stream.samples == nullptr) {
                continue;
            }
            const float* source = planar[channel].data();
            float* destination = stream.samples + (static_cast<std::size_t>(offset) * width) + inStream;
            for (Frames frame = 0; frame < frames; ++frame) {
                destination[static_cast<std::size_t>(frame) * width] = source[frame];
            }
        }
    }
}

}  // namespace

PlanarAdapter::PlanarAdapter(int inputChannels, int outputChannels, Frames maxFrames)
    : maxFrames_(maxFrames),
      inputs_(static_cast<std::size_t>(inputChannels), std::vector<float>(static_cast<std::size_t>(maxFrames))),
      outputs_(static_cast<std::size_t>(outputChannels), std::vector<float>(static_cast<std::size_t>(maxFrames))) {
    for (const std::vector<float>& buffer : inputs_) {
        inputPointers_.push_back(buffer.data());
    }
    for (std::vector<float>& buffer : outputs_) {
        outputPointers_.push_back(buffer.data());
    }
}

void PlanarAdapter::process(std::span<const InterleavedInput> inputs, std::span<const InterleavedOutput> outputs,
                            Frames frames, std::int64_t hostTimeNs, double sampleRate, const AudioCallback& callback) {
    for (Frames done = 0; done < frames;) {
        const Frames block = std::min(frames - done, maxFrames_);
        deinterleave(inputs, done, block, inputs_);
        callback({block, hostTimeNs + framesToNs(done, sampleRate), inputPointers_, outputPointers_});
        interleave(outputs_, done, block, outputs);
        done += block;
    }
}

void flushDenormalsToZero() {
#ifdef __aarch64__
    constexpr std::uint64_t kFlushToZero = std::uint64_t{1} << 24;
    std::uint64_t control = 0;
    asm volatile("mrs %0, fpcr" : "=r"(control));
    asm volatile("msr fpcr, %0" : : "r"(control | kFlushToZero));
#elifdef __x86_64__
    constexpr unsigned kFlushToZeroAndDenormalsAreZero = 0x8040;
    _mm_setcsr(_mm_getcsr() | kFlushToZeroAndDenormalsAreZero);
#endif
}

}  // namespace emerick::engine
